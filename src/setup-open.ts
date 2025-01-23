import {
  ConcurrentLockType,
  ContextLockID,
  ISQLite,
  LockContext,
  LockOptions,
  OpenOptions,
  QueryResult,
  QuickSQLiteConnection,
  SQLBatchTuple,
  TransactionContext,
  UpdateCallback
} from './types';

import { DBListenerManagerInternal } from './DBListenerManager';
import { LockHooks } from './lock-hooks';
import { enhanceQueryResult } from './utils';

type LockCallbackRecord = {
  callback: (context: LockContext) => Promise<any>;
  timeout?: NodeJS.Timeout;
};

enum TransactionFinalizer {
  COMMIT = 'commit',
  ROLLBACK = 'rollback'
}

const DEFAULT_READ_CONNECTIONS = 4;

// A incrementing integer ID for tracking lock requests
let requestIdCounter = 1;

const getRequestId = () => {
  requestIdCounter++;
  return `${requestIdCounter}`;
};

const LockCallbacks: Record<ContextLockID, LockCallbackRecord> = {};
let proxy: ISQLite;

/**
 * Creates a unique identifier for all a database's lock requests
 */
const lockKey = (dbName: string, id: ContextLockID) => `${dbName}:${id}`;

/**
 * Closes the context in JS and C++
 */
function closeContextLock(dbName: string, id: ContextLockID) {
  delete LockCallbacks[lockKey(dbName, id)];

  // This is configured by the setupOpen function
  proxy.releaseLock(dbName, id);
}

/**
 * JS callback to trigger queued callbacks when a lock context is available.
 * Declared on the global scope so that C++ can call it.
 * @param lockId
 * @returns
 */
global.onLockContextIsAvailable = async (dbName: string, lockId: ContextLockID) => {
  // Don't hold C++ bridge side up waiting to complete
  setImmediate(async () => {
    try {
      const key = lockKey(dbName, lockId);
      const record = LockCallbacks[key];
      // clear record after fetching, the hash should only contain pending requests
      delete LockCallbacks[key];

      if (record?.timeout) {
        clearTimeout(record.timeout);
      }
      await record?.callback({
        // @ts-expect-error This is not part of the public interface, but is used internally
        _contextId: lockId,
        execute: async (sql: string, args?: any[]) => {
          const result = await proxy.executeInContext(dbName, lockId, sql, args);
          enhanceQueryResult(result);
          return result;
        }
      });
    } catch (ex) {
      console.error(ex);
    }
  });
};

/**
 * Generates the entry point for opening concurrent connections
 * @param proxy
 * @returns
 */
export function setupOpen(QuickSQLite: ISQLite) {
  // Allow the Global callbacks to close lock contexts
  proxy = QuickSQLite;

  return {
    /**
     * Opens a SQLite DB connection.
     * By default opens DB in WAL mode with 4 Read connections and a single
     * write connection
     */
    open: (dbName: string, options: OpenOptions = {}): QuickSQLiteConnection => {
      // Opens the connection
      QuickSQLite.open(dbName, {
        ...options,
        numReadConnections: options?.numReadConnections ?? DEFAULT_READ_CONNECTIONS
      });

      const listenerManager = new DBListenerManagerInternal({ dbName });

      /**
       * Wraps lock requests and their callbacks in order to resolve the lock
       * request with the callback result once triggered from the connection pool.
       */
      const requestLock = <T>(
        type: ConcurrentLockType,
        callback: (context: LockContext) => Promise<T>,
        options?: LockOptions,
        hooks?: LockHooks
      ): Promise<T> => {
        const id = getRequestId();
        // Wrap the callback in a promise that will resolve to the callback result
        return new Promise<T>((resolve, reject) => {
          // Add callback to the queue for timing
          const key = lockKey(dbName, id);
          const record = (LockCallbacks[key] = {
            callback: async (context: LockContext) => {
              try {
                await hooks?.lockAcquired?.();
                const res = await callback(context);
                closeContextLock(dbName, id);
                resolve(res);
              } catch (ex) {
                closeContextLock(dbName, id);
                reject(ex);
              } finally {
                hooks?.lockReleased?.();
              }
            }
          } as LockCallbackRecord);

          try {
            // throws if lock could not be requested
            QuickSQLite.requestLock(dbName, id, type);
            const timeout = options?.timeoutMs;
            if (timeout) {
              record.timeout = setTimeout(() => {
                // The callback won't be executed
                delete LockCallbacks[key];
                reject(new Error(`Lock request timed out after ${timeout}ms`));
              }, timeout);
            }
          } catch (ex) {
            // Remove callback from the queue
            delete LockCallbacks[key];
            reject(ex);
          }
        });
      };

      const readLock = <T>(callback: (context: LockContext) => Promise<T>, options?: LockOptions): Promise<T> =>
        requestLock(ConcurrentLockType.READ, callback, options);

      const writeLock = <T>(callback: (context: LockContext) => Promise<T>, options?: LockOptions): Promise<T> =>
        requestLock(ConcurrentLockType.WRITE, callback, options, {
          lockReleased: async () => {
            // flush updates once a write lock has been released
            listenerManager.flushUpdates();
          }
        });

      const wrapTransaction = async <T>(
        context: LockContext,
        callback: (context: TransactionContext) => Promise<T>,
        defaultFinalizer: TransactionFinalizer = TransactionFinalizer.COMMIT
      ) => {
        await context.execute('BEGIN TRANSACTION');
        let finalized = false;

        const finalizedStatement =
          <T>(action: () => T): (() => T) =>
          () => {
            if (finalized) {
              return;
            }
            finalized = true;
            return action();
          };

        const commit = finalizedStatement(async () => context.execute('COMMIT'));

        const rollback = finalizedStatement(async () => context.execute('ROLLBACK'));

        const wrapExecute =
          <T>(
            method: (sql: string, params?: any[]) => Promise<QueryResult>
          ): ((sql: string, params?: any[]) => Promise<QueryResult>) =>
          async (sql: string, params?: any[]) => {
            if (finalized) {
              throw new Error(`Cannot execute in transaction after it has been finalized with commit/rollback.`);
            }
            return method(sql, params);
          };

        try {
          const res = await callback({
            ...context,
            commit,
            rollback,
            execute: wrapExecute(context.execute)
          });
          switch (defaultFinalizer) {
            case TransactionFinalizer.COMMIT:
              await commit();
              break;
            case TransactionFinalizer.ROLLBACK:
              await rollback();
              break;
          }
          return res;
        } catch (ex) {
          try {
            await rollback();
          } catch (ex2) {
            // In rare cases, a rollback may fail.
            // Safe to ignore.
          }
          throw ex;
        }
      };

      // Return the concurrent connection object
      return {
        close: () => {
          QuickSQLite.close(dbName);
          // Reject any pending lock requests
          Object.entries(LockCallbacks).forEach(([key, record]) => {
            const recordDBName = key.split(':')[0];
            if (dbName !== recordDBName) {
              return;
            }
            // A bit of a hack, let the callbacks run with an execute method that will fail
            record
              .callback({
                execute: async () => {
                  throw new Error('Connection is closed');
                }
              })
              .catch(() => {});

            delete LockCallbacks[key];
          });
        },
        refreshSchema: () => QuickSQLite.refreshSchema(dbName),
        execute: (sql: string, args?: any[]) => writeLock((context) => context.execute(sql, args)),
        readLock,
        readTransaction: async <T>(callback: (context: TransactionContext) => Promise<T>, options?: LockOptions) =>
          readLock((context) => wrapTransaction(context, callback)),
        writeLock,
        writeTransaction: async <T>(callback: (context: TransactionContext) => Promise<T>, options?: LockOptions) =>
          writeLock((context) => wrapTransaction(context, callback, TransactionFinalizer.COMMIT), options),
        delete: () => QuickSQLite.delete(dbName, options?.location),
        executeBatch: (commands: SQLBatchTuple[]) =>
          writeLock((context) => QuickSQLite.executeBatch(dbName, commands, (context as any)._contextId)),
        attach: (dbNameToAttach: string, alias: string, location?: string) =>
          QuickSQLite.attach(dbName, dbNameToAttach, alias, location),
        detach: (alias: string) => QuickSQLite.detach(dbName, alias),
        loadFile: (location: string) =>
          writeLock((context) => QuickSQLite.loadFile(dbName, location, (context as any)._contextId)),
        listenerManager,
        registerUpdateHook: (callback: UpdateCallback) =>
          listenerManager.registerListener({ rawTableChange: callback }),
        registerTablesChangedHook: (callback) => listenerManager.registerListener({ tablesUpdated: callback })
      };
    }
  };
}
