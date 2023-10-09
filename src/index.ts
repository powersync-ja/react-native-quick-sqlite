import { NativeModules } from 'react-native';
import {
  RowUpdateType,
  Transaction,
  QueryResult,
  SQLBatchTuple,
  PendingTransaction,
  UpdateCallback,
  ISQLite,
  ConcurrentISQLite,
  QuickSQLiteConnection
} from './types';
import { setupConcurrency } from './ConcurrentConnection';
import { enhanceQueryResult } from './utils';
import { registerUpdateHook } from './table-updates';

export * from './types';

declare global {
  function nativeCallSyncHook(): unknown;
  var __QuickSQLiteProxy: object | undefined;
}

if (global.__QuickSQLiteProxy == null) {
  const QuickSQLiteModule = NativeModules.QuickSQLite;

  if (QuickSQLiteModule == null) {
    throw new Error('Base quick-sqlite module not found. Maybe try rebuilding the app.');
  }

  // Check if we are running on-device (JSI)
  if (global.nativeCallSyncHook == null || QuickSQLiteModule.install == null) {
    throw new Error(
      'Failed to install react-native-quick-sqlite: React Native is not running on-device. QuickSQLite can only be used when synchronous method invocations (JSI) are possible. If you are using a remote debugger (e.g. Chrome), switch to an on-device debugger (e.g. Flipper) instead.'
    );
  }

  // Call the synchronous blocking install() function
  const result = QuickSQLiteModule.install();
  if (result !== true) {
    throw new Error(
      `Failed to install react-native-quick-sqlite: The native QuickSQLite Module could not be installed! Looks like something went wrong when installing JSI bindings: ${result}`
    );
  }

  // Check again if the constructor now exists. If not, throw an error.
  if (global.__QuickSQLiteProxy == null) {
    throw new Error(
      'Failed to install react-native-quick-sqlite, the native initializer function does not exist. Are you trying to use QuickSQLite from different JS Runtimes?'
    );
  }
}

const proxy = global.__QuickSQLiteProxy;
export const QuickSQLite = proxy as ISQLite;

const locks: Record<string, { queue: PendingTransaction[]; inProgress: boolean }> = {};

// Enhance some host functions

const _open = QuickSQLite.open;
QuickSQLite.open = (dbName: string, location?: string) => {
  _open(dbName, location);

  // Always make a lock, even if concurrency is open, this helps prevent the same
  // DB being opened multiple times (externally)
  locks[dbName] = {
    queue: [],
    inProgress: false
  };
};

const _close = QuickSQLite.close;
QuickSQLite.close = (dbName: string) => {
  _close(dbName);
  delete locks[dbName];
};

const _execute = QuickSQLite.execute;
QuickSQLite.execute = (dbName: string, query: string, params?: any[] | undefined): QueryResult => {
  const result = _execute(dbName, query, params);
  enhanceQueryResult(result);
  return result;
};

const _executeAsync = QuickSQLite.executeAsync;
QuickSQLite.executeAsync = async (dbName: string, query: string, params?: any[] | undefined): Promise<QueryResult> => {
  const res = await _executeAsync(dbName, query, params);
  enhanceQueryResult(res);
  return res;
};

QuickSQLite.transaction = async (dbName: string, fn: (tx: Transaction) => Promise<void>): Promise<void> => {
  if (!locks[dbName]) {
    throw Error(`Quick SQLite Error: No lock found on db: ${dbName}`);
  }

  let isFinalized = false;

  // Local transaction context object implementation
  const execute = (query: string, params?: any[]): QueryResult => {
    if (isFinalized) {
      throw Error(`Quick SQLite Error: Cannot execute query on finalized transaction: ${dbName}`);
    }
    return QuickSQLite.execute(dbName, query, params);
  };

  const executeAsync = (query: string, params?: any[] | undefined) => {
    if (isFinalized) {
      throw Error(`Quick SQLite Error: Cannot execute query on finalized transaction: ${dbName}`);
    }
    return QuickSQLite.executeAsync(dbName, query, params);
  };

  const commit = () => {
    if (isFinalized) {
      throw Error(`Quick SQLite Error: Cannot execute commit on finalized transaction: ${dbName}`);
    }
    const result = QuickSQLite.execute(dbName, 'COMMIT');
    isFinalized = true;
    return result;
  };

  const commitAsync = async () => {
    if (isFinalized) {
      throw Error(`Quick SQLite Error: Cannot execute commit on finalized transaction: ${dbName}`);
    }
    const result = await QuickSQLite.executeAsync(dbName, 'COMMIT');
    isFinalized = true;
    return result;
  };

  const rollback = () => {
    if (isFinalized) {
      throw Error(`Quick SQLite Error: Cannot execute rollback on finalized transaction: ${dbName}`);
    }
    const result = QuickSQLite.execute(dbName, 'ROLLBACK');
    isFinalized = true;
    return result;
  };

  const rollbackAsync = async () => {
    if (isFinalized) {
      throw Error(`Quick SQLite Error: Cannot execute rollback on finalized transaction: ${dbName}`);
    }
    const result = await QuickSQLite.executeAsync(dbName, 'ROLLBACK');
    isFinalized = true;
    return result;
  };

  async function run() {
    try {
      await QuickSQLite.executeAsync(dbName, 'BEGIN TRANSACTION');

      await fn({
        commit,
        commitAsync,
        execute,
        executeAsync,
        rollback,
        rollbackAsync
      });

      if (!isFinalized) {
        commit();
      }
    } catch (executionError) {
      if (!isFinalized) {
        try {
          rollback();
        } catch (rollbackError) {
          throw rollbackError;
        }
      }

      throw executionError;
    } finally {
      locks[dbName].inProgress = false;
      isFinalized = false;
      startNextTransaction(dbName);
    }
  }

  return await new Promise((resolve, reject) => {
    const tx: PendingTransaction = {
      start: () => {
        run().then(resolve).catch(reject);
      }
    };

    locks[dbName].queue.push(tx);
    startNextTransaction(dbName);
  });
};

const startNextTransaction = (dbName: string) => {
  if (!locks[dbName]) {
    throw Error(`Lock not found for db: ${dbName}`);
  }

  if (locks[dbName].inProgress) {
    // Transaction is already in process bail out
    return;
  }

  if (locks[dbName].queue.length) {
    locks[dbName].inProgress = true;
    const tx = locks[dbName].queue.shift();
    setImmediate(() => {
      tx.start();
    });
  }
};

//   _________     _______  ______ ____  _____  __  __            _____ _____
//  |__   __\ \   / /  __ \|  ____/ __ \|  __ \|  \/  |     /\   |  __ \_   _|
//     | |   \ \_/ /| |__) | |__ | |  | | |__) | \  / |    /  \  | |__) || |
//     | |    \   / |  ___/|  __|| |  | |  _  /| |\/| |   / /\ \ |  ___/ | |
//     | |     | |  | |    | |___| |__| | | \ \| |  | |  / ____ \| |    _| |_
//     |_|     |_|  |_|    |______\____/|_|  \_\_|  |_| /_/    \_\_|   |_____|

/**
 * DO NOT USE THIS! THIS IS MEANT FOR TYPEORM
 * If you are looking for a convenience wrapper use `connect`
 */
export const typeORMDriver = {
  openDatabase: (
    options: {
      name: string;
      location?: string;
    },
    ok: (db: any) => void,
    fail: (msg: string) => void
  ): any => {
    try {
      QuickSQLite.open(options.name, options.location);

      const connection = {
        executeSql: async (
          sql: string,
          params: any[] | undefined,
          ok: (res: QueryResult) => void,
          fail: (msg: string) => void
        ) => {
          try {
            let response = await QuickSQLite.executeAsync(options.name, sql, params);
            enhanceQueryResult(response);
            ok(response);
          } catch (e) {
            fail(e);
          }
        },
        transaction: (fn: (tx: Transaction) => Promise<void>): Promise<void> => {
          return QuickSQLite.transaction(options.name, fn);
        },
        close: (ok: any, fail: any) => {
          try {
            QuickSQLite.close(options.name);
            ok();
          } catch (e) {
            fail(e);
          }
        },
        attach: (dbNameToAttach: string, alias: string, location: string | undefined, callback: () => void) => {
          QuickSQLite.attach(options.name, dbNameToAttach, alias, location);

          callback();
        },
        detach: (alias, callback: () => void) => {
          QuickSQLite.detach(options.name, alias);

          callback();
        }
      };

      ok(connection);

      return connection;
    } catch (e) {
      fail(e);
    }
  }
};

export const { openConcurrent } = setupConcurrency(QuickSQLite as ConcurrentISQLite);

export const open = (options: { name: string; location?: string }): QuickSQLiteConnection => {
  QuickSQLite.open(options.name, options.location);

  return {
    close: () => QuickSQLite.close(options.name),
    delete: () => QuickSQLite.delete(options.name, options.location),
    attach: (dbNameToAttach: string, alias: string, location?: string) =>
      QuickSQLite.attach(options.name, dbNameToAttach, alias, location),
    detach: (alias: string) => QuickSQLite.detach(options.name, alias),
    transaction: (fn: (tx: Transaction) => Promise<void> | void) => QuickSQLite.transaction(options.name, fn),
    execute: (query: string, params?: any[] | undefined): QueryResult =>
      QuickSQLite.execute(options.name, query, params),
    executeAsync: (query: string, params?: any[] | undefined): Promise<QueryResult> =>
      QuickSQLite.executeAsync(options.name, query, params),
    executeBatch: (commands: SQLBatchTuple[]) => QuickSQLite.executeBatch(options.name, commands),
    executeBatchAsync: (commands: SQLBatchTuple[]) => QuickSQLite.executeBatchAsync(options.name, commands),
    loadFile: (location: string) => QuickSQLite.loadFile(options.name, location),
    loadFileAsync: (location: string) => QuickSQLite.loadFileAsync(options.name, location),
    registerUpdateHook: (callback: UpdateCallback) => {
      registerUpdateHook(options.name, callback);
    }
  };
};
