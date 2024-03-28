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
  OpenOptions,
  QueryResult,
} from './types';

import uuid from 'uuid';
import _ from 'lodash';
import { enhanceQueryResult } from './utils';
import { DBListenerManagerInternal } from './DBListenerManager';
import { LockHooks } from './lock-hooks';

type LockCallbackRecord = {
  callback: (context: LockContext) => Promise<any>;
  timeout?: NodeJS.Timeout;
};

enum TransactionFinalizer {
  COMMIT = 'commit',
  ROLLBACK = 'rollback',
}

const DEFAULT_READ_CONNECTIONS = 4;

const getNow = () => new Date().valueOf() % 1000000;
const logEvent = (name: string) => console.log(`LOG: ${name} --- ${getNow()}`);

const LockCallbacks: Record<ContextLockID, LockCallbackRecord> = {};
let proxy: ISQLite;

/**
 * Closes the context in JS and C++
 */
function closeContextLock(dbName: string, id: ContextLockID) {
  delete LockCallbacks[id];

  logEvent('before calling proxy.releaseLock');
  // This is configured by the setupOpen function
  proxy.releaseLock(dbName, id);
  logEvent('after calling proxy.releaseLock');
}

/**
 * JS callback to trigger queued callbacks when a lock context is available.
 * Declared on the global scope so that C++ can call it.
 * @param lockId
 * @returns
 */
global.onLockContextIsAvailable = async (
  dbName: string,
  lockId: ContextLockID
) => {
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
        execute: async (sql: string, args?: any[]) => {
          const result = await proxy.executeInContext(
            dbName,
            lockId,
            sql,
            args
          );
          enhanceQueryResult(result);
          return result;
        },
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
    open: (
      dbName: string,
      options: OpenOptions = {}
    ): QuickSQLiteConnection => {
      // Opens the connection
      QuickSQLite.open(dbName, {
        ...options,
        numReadConnections:
          options?.numReadConnections ?? DEFAULT_READ_CONNECTIONS,
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
        logEvent('requestLock start');
        let start = performance.now();
        const id = uuid.v4(); // TODO maybe do this in C++
        logEvent('requestLock uuid generated');

        let end = performance.now();
        console.log(`Gen uuid.v4() ${end - start}`);
        // Wrap the callback in a promise that will resolve to the callback result
        return new Promise<T>((resolve, reject) => {
          // Add callback to the queue for timing
          const record = (LockCallbacks[id] = {
            callback: async (context: LockContext) => {
              try {
                start = performance.now();
                logEvent('[lockCallback] Before executing lock aquired');
                await hooks?.lockAcquired?.();
                logEvent('[lockCallback] After executing lock aquired');

                end = performance.now();
                console.log(`hooks?.lockAcquired ${end - start}`);
                start = performance.now();
                logEvent('[lockCallback] Before calling callback');
                const res = await callback(context);
                logEvent('[lockCallback] After calling callback');
                end = performance.now();
                console.log(`requestLock -> await callback() ${end - start}`);
                // Ensure that we only resolve after locks are freed
                _.defer(() => resolve(res));
                console.log(`requestLock -> resolve ${end - start}`);
              } catch (ex) {
                _.defer(() => reject(ex));
              } finally {
                _.defer(() => hooks?.lockReleased?.());
                end = performance.now();
                console.log(`requestLock -> finally ${end - start}`);
              }
            },
          } as LockCallbackRecord);

          try {
            start = performance.now();
            logEvent('Before calling QuickSQLite.requestLock');
            QuickSQLite.requestLock(dbName, id, type);
            logEvent('After calling QuickSQLite.requestLock');
            end = performance.now();
            console.log(`QuickSQLite.requestLock() ${end - start}`);
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

      const readLock = <T>(
        callback: (context: LockContext) => Promise<T>,
        options?: LockOptions
      ): Promise<T> => requestLock(ConcurrentLockType.READ, callback, options);

      const writeLock = <T>(
        callback: (context: LockContext) => Promise<T>,
        options?: LockOptions
      ): Promise<T> => {
        console.log('Starting write lock');
        let start = performance.now();
        return requestLock(ConcurrentLockType.WRITE, callback, options, {
          lockReleased: async () => {
            logEvent('In lockReleased callback');
            // flush updates once a write lock has been released
            logEvent('In lockReleased callback: before flushing updates');
            listenerManager.flushUpdates();
            logEvent('In lockReleased callback: after flushing updates');
            let end = performance.now();
            console.log(`Flushing write lock ${end - start}`);
          },
        });
      };

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

        const commit = finalizedStatement(async () =>
          context.execute('COMMIT')
        );

        const rollback = finalizedStatement(async () =>
          context.execute('ROLLBACK')
        );

        const wrapExecute =
          <T>(
            method: (sql: string, params?: any[]) => Promise<QueryResult>
          ): ((sql: string, params?: any[]) => Promise<QueryResult>) =>
          async (sql: string, params?: any[]) => {
            if (finalized) {
              throw new Error(
                `Cannot execute in transaction after it has been finalized with commit/rollback.`
              );
            }
            return method(sql, params);
          };

        try {
          const res = await callback({
            ...context,
            commit,
            rollback,
            execute: wrapExecute(context.execute),
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
          await rollback();
          throw ex;
        }
      };

      // Return the concurrent connection object
      return {
        close: () => QuickSQLite.close(dbName),
        execute: (sql: string, args?: any[]) =>
          writeLock((context) => context.execute(sql, args)),
        readLock,
        readTransaction: async <T>(
          callback: (context: TransactionContext) => Promise<T>,
          options?: LockOptions
        ) => readLock((context) => wrapTransaction(context, callback)),
        writeLock,
        writeTransaction: async <T>(
          callback: (context: TransactionContext) => Promise<T>,
          options?: LockOptions
        ) =>
          writeLock(
            (context) =>
              wrapTransaction(context, callback, TransactionFinalizer.COMMIT),
            options
          ),
        delete: () => QuickSQLite.delete(dbName, options?.location),
        executeBatch: (commands: SQLBatchTuple[]) =>
          writeLock((context) =>
            QuickSQLite.executeBatch(
              dbName,
              commands,
              (context as any)._contextId
            )
          ),
        attach: (dbNameToAttach: string, alias: string, location?: string) =>
          QuickSQLite.attach(dbName, dbNameToAttach, alias, location),
        detach: (alias: string) => QuickSQLite.detach(dbName, alias),
        loadFile: (location: string) =>
          writeLock((context) =>
            QuickSQLite.loadFile(dbName, location, (context as any)._contextId)
          ),
        listenerManager,
        registerUpdateHook: (callback: UpdateCallback) =>
          listenerManager.registerListener({ rawTableChange: callback }),
        registerTablesChangedHook: (callback) =>
          listenerManager.registerListener({ tablesUpdated: callback }),
      };
    },
  };
}
