import {
  ISQLite,
  ConcurrentLockType,
  QuickSQLiteConnection,
  ContextLockID,
  LockContext,
  LockOptions,
  TransactionContext,
  UpdateCallback,
  SQLBatchTuple,
  OpenOptions
} from './types';

import uuid from 'uuid';
import { enhanceQueryResult } from './utils';
import { registerUpdateHook } from './table-updates';

type LockCallbackRecord = {
  callback: (context: LockContext) => Promise<any>;
  timeout?: NodeJS.Timeout;
};

const DEFAULT_READ_CONNECTIONS = 4;

const LockCallbacks: Record<ContextLockID, LockCallbackRecord> = {};
let proxy: ISQLite;

/**
 * Closes the context in JS and C++
 */
function closeContextLock(dbName: string, id: ContextLockID) {
  delete LockCallbacks[id];

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
      const record = LockCallbacks[lockId];
      if (record?.timeout) {
        clearTimeout(record.timeout);
      }
      await record?.callback({
        // @ts-expect-error This is not part of the public interface, but is used internally
        _contextId: lockId,
        execute: (sql: string, args?: any[]) => {
          const result = proxy.executeInContext(dbName, lockId, sql, args);
          enhanceQueryResult(result);
          return result;
        },
        executeAsync: async (sql: string, args?: any[]) => {
          const result = await proxy.executeInContextAsync(dbName, lockId, sql, args);
          enhanceQueryResult(result);
          return result;
        }
      });
    } catch (ex) {
      console.error(ex);
    } finally {
      // Always release a lock once finished
      closeContextLock(dbName, lockId);
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

      /**
       * Wraps lock requests and their callbacks in order to resolve the lock
       * request with the callback result once triggered from the connection pool.
       */
      const requestLock = <T>(
        type: ConcurrentLockType,
        callback: (context: LockContext) => Promise<T>,
        options?: LockOptions
      ): Promise<T> => {
        const id = uuid.v4(); // TODO maybe do this in C++
        // Wrap the callback in a promise that will resolve to the callback result
        return new Promise<T>((resolve, reject) => {
          // Add callback to the queue for timing
          const record = (LockCallbacks[id] = {
            callback: async (context: LockContext) => {
              try {
                const res = await callback(context);
                resolve(res);
              } catch (ex) {
                reject(ex);
              }
            }
          } as LockCallbackRecord);

          try {
            QuickSQLite.requestLock(dbName, id, type);
            const timeout = options?.timeoutMs;
            if (timeout) {
              record.timeout = setTimeout(() => {
                // The callback won't be executed
                delete LockCallbacks[id];
                reject(new Error(`Lock request timed out after ${timeout}ms`));
              }, timeout);
            }
          } catch (ex) {
            // Remove callback from the queue
            delete LockCallbacks[id];
            reject(ex);
          }
        });
      };

      const readLock = <T>(callback: (context: LockContext) => Promise<T>, options?: LockOptions): Promise<T> =>
        requestLock(ConcurrentLockType.READ, callback, options);

      const writeLock = <T>(callback: (context: LockContext) => Promise<T>, options?: LockOptions): Promise<T> =>
        requestLock(ConcurrentLockType.WRITE, callback, options);

      const wrapTransaction = async <T>(
        context: LockContext,
        callback: (context: TransactionContext) => Promise<T>,
        defaultFinally: 'commit' | 'rollback' = 'commit'
      ) => {
        await context.executeAsync('BEGIN TRANSACTION');
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

        const commit = finalizedStatement(() => context.execute('COMMIT'));
        const commitAsync = finalizedStatement(() => context.executeAsync('COMMIT'));

        const rollback = finalizedStatement(() => context.execute('ROLLBACK'));
        const rollbackAsync = finalizedStatement(() => context.executeAsync('ROLLBACK'));

        const wrapExecute =
          <T>(method: (sql: string, params?: any[]) => T): ((sql: string, params?: any[]) => T) =>
          (sql: string, params?: any[]) => {
            if (finalized) {
              throw new Error(`Cannot execute in transaction after it has been finalized with commit/rollback.`);
            }
            return method(sql, params);
          };

        try {
          const res = await callback({
            ...context,
            commit,
            commitAsync,
            rollback,
            rollbackAsync,
            execute: wrapExecute(context.execute),
            executeAsync: wrapExecute(context.executeAsync)
          });
          switch (defaultFinally) {
            case 'commit':
              await commitAsync();
              break;
            case 'rollback':
              await rollbackAsync();
              break;
          }
          return res;
        } catch (ex) {
          await rollbackAsync();
          throw ex;
        }
      };

      // Return the concurrent connection object
      return {
        close: () => QuickSQLite.close(dbName),
        execute: (sql: string, args?: any[]) => writeLock((context) => context.executeAsync(sql, args)),
        readLock,
        readTransaction: async <T>(callback: (context: TransactionContext) => Promise<T>, options?: LockOptions) =>
          readLock((context) => wrapTransaction(context, callback)),
        writeLock,
        writeTransaction: async <T>(callback: (context: TransactionContext) => Promise<T>, options?: LockOptions) =>
          writeLock((context) => wrapTransaction(context, callback), options),
        registerUpdateHook: (callback: UpdateCallback) => {
          registerUpdateHook(dbName, callback);
        },
        delete: () => QuickSQLite.delete(dbName, options?.location),
        executeBatchAsync: (commands: SQLBatchTuple[]) =>
          writeLock((context) => QuickSQLite.executeBatchAsync(dbName, commands, (context as any)._contextId)),
        attach: (dbNameToAttach: string, alias: string, location?: string) =>
          QuickSQLite.attach(dbName, dbNameToAttach, alias, location),
        detach: (alias: string) => QuickSQLite.detach(dbName, alias),
        loadFileAsync: (location: string) => writeLock((context) => QuickSQLite.loadFileAsync(dbName, location))
      };
    }
  };
}
