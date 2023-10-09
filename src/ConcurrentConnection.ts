import {
  ConcurrentISQLite,
  ConcurrentLockType,
  ConcurrentQuickSQLiteConnection,
  ContextLockID,
  LockContext,
  LockOptions,
  TransactionContext,
  UpdateCallback
} from './types';

import uuid from 'uuid';
import { enhanceQueryResult } from './utils';
import { registerUpdateHook } from './table-updates';

type LockCallbackRecord = {
  callback: (context: LockContext) => Promise<any>;
  timeout?: NodeJS.Timeout;
};

const LockCallbacks: Record<ContextLockID, LockCallbackRecord> = {};
let proxy: ConcurrentISQLite;

/**
 * Closes the context in JS and C++
 */
function closeContextLock(dbName: string, id: ContextLockID) {
  delete LockCallbacks[id];

  // This is configured by the setupConcurrency function
  proxy.releaseConcurrentLock(dbName, id);
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
      if (record.timeout) {
        clearTimeout(record.timeout);
      }
      await record?.callback({
        execute: async (sql: string, args?: any[]) => {
          const result = await proxy.executeInContext(dbName, lockId, sql, args);
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
export function setupConcurrency(QuickSQLite: ConcurrentISQLite) {
  // Allow the Global callbacks to close lock contexts
  proxy = QuickSQLite;

  return {
    openConcurrent: (dbName: string, location?: string): ConcurrentQuickSQLiteConnection => {
      // Opens the connection
      QuickSQLite.openConcurrent(dbName, location);

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
        let resolve: (value: T) => void;
        let reject: (ex: any) => void;

        const promise = new Promise<T>((res, rej) => {
          resolve = res;
          reject = rej;
        });

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
          QuickSQLite.requestConcurrentLock(dbName, id, type);
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

        return promise;
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
        await context.execute('BEGIN TRANSACTION');
        let finalized = false;

        const commit = async () => {
          if (finalized) {
            return;
          }
          finalized = true;
          return context.execute('COMMIT');
        };

        const rollback = async () => {
          if (finalized) {
            return;
          }
          finalized = true;
          return context.execute('ROLLBACK');
        };

        try {
          const res = await callback({ ...context, commit, rollback });
          switch (defaultFinally) {
            case 'commit':
              await commit();
              break;
            case 'rollback':
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
        close: () => QuickSQLite.closeConcurrent(dbName),
        execute: (sql: string, args?: any[]) => writeLock((context) => context.execute(sql, args)),
        readLock,
        readTransaction: async <T>(callback: (context: TransactionContext) => Promise<T>, options?: LockOptions) =>
          readLock((context) => wrapTransaction(context, callback)),
        writeLock,
        writeTransaction: async <T>(callback: (context: TransactionContext) => Promise<T>, options?: LockOptions) =>
          writeLock((context) => wrapTransaction(context, callback), options),
        registerUpdateHook: (callback: UpdateCallback) => {
          registerUpdateHook(dbName, callback);
        }
      };
    }
  };
}
