//   _________     _______  ______ ____  _____  __  __            _____ _____
//  |__   __\ \   / /  __ \|  ____/ __ \|  __ \|  \/  |     /\   |  __ \_   _|
//     | |   \ \_/ /| |__) | |__ | |  | | |__) | \  / |    /  \  | |__) || |
//     | |    \   / |  ___/|  __|| |  | |  _  /| |\/| |   / /\ \ |  ___/ | |
//     | |     | |  | |    | |___| |__| | | \ \| |  | |  / ____ \| |    _| |_
//     |_|     |_|  |_|    |______\____/|_|  \_\_|  |_| /_/    \_\_|   |_____|

import { QueryResult, TransactionContext, Open } from './types';

/**
 * DO NOT USE THIS! THIS IS MEANT FOR TYPEORM
 * If you are looking for a convenience wrapper use `connect`
 */
export const setupTypeORMDriver = (open: Open) => ({
  openDatabase: (
    options: {
      name: string;
      location?: string;
    },
    ok: (db: any) => void,
    fail: (msg: string) => void
  ): any => {
    try {
      const _con = open(options.name, { location: options.location });

      const connection = {
        executeSql: async (
          sql: string,
          params: any[] | undefined,
          ok: (res: QueryResult) => void,
          fail: (msg: string) => void
        ) => {
          try {
            let response = await _con.execute(sql, params);
            ok(response);
          } catch (e) {
            fail(e);
          }
        },
        transaction: (
          fn: (tx: TransactionContext) => Promise<void>
        ): Promise<void> => {
          return _con.writeTransaction(fn);
        },
        close: (ok: any, fail: any) => {
          try {
            _con.close();
            ok();
          } catch (e) {
            fail(e);
          }
        },
        attach: (
          dbNameToAttach: string,
          alias: string,
          location: string | undefined,
          callback: () => void
        ) => {
          _con.attach(dbNameToAttach, alias, location);
          callback();
        },
        detach: (alias, callback: () => void) => {
          _con.detach(alias);
          callback();
        },
      };

      ok(connection);

      return connection;
    } catch (e) {
      fail(e);
    }
  },
});
