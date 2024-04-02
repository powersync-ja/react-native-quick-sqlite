import Chance from 'chance';
import {
  BatchedUpdateNotification,
  open,
  QueryResult,
  QuickSQLite,
  QuickSQLiteConnection,
  SQLBatchTuple,
  TransactionEvent,
  UpdateNotification,
} from 'react-native-quick-sqlite';
import { beforeEach, describe, it } from '../mocha/MochaRNAdapter';
import chai from 'chai';

const { expect } = chai;
const chance = new Chance();

let db: QuickSQLiteConnection = global.db;

const NUM_READ_CONNECTIONS = 3;

function randomIntFromInterval(min: number, max: number) {
  // min included and max excluded
  return Math.random() * (max - min) + min;
}

const digits = [
  '',
  'one',
  'two',
  'three',
  'four',
  'five',
  'six',
  'seven',
  'eight',
  'nine',
];
const names100: string[] = [
  ...digits,
  ...[
    'ten',
    'eleven',
    'twelve',
    'thirteen',
    'fourteen',
    'fifteen',
    'sixteen',
    'seventeen',
    'eighteen',
    'nineteen',
  ],
  ...digits.map((digit) => `twenty${digit != '' ? '-' + digit : ''}`),
  ...digits.map((digit) => `thirty${digit != '' ? '-' + digit : ''}`),
  ...digits.map((digit) => `forty${digit != '' ? '-' + digit : ''}`),
  ...digits.map((digit) => `fifty${digit != '' ? '-' + digit : ''}`),
  ...digits.map((digit) => `sixty${digit != '' ? '-' + digit : ''}`),
  ...digits.map((digit) => `seventy${digit != '' ? '-' + digit : ''}`),
  ...digits.map((digit) => `eighty${digit != '' ? '-' + digit : ''}`),
  ...digits.map((digit) => `ninety${digit != '' ? '-' + digit : ''}`),
];

function numberName(n: number) {
  if (n == 0) {
    return 'zero';
  }

  let numberName: string[] = [];
  let d43 = Math.floor(n / 1000);
  if (d43 != 0) {
    numberName.push(names100[d43]);
    numberName.push('thousand');
    n -= d43 * 1000;
  }

  let d2 = Math.floor(n / 100);
  if (d2 != 0) {
    numberName.push(names100[d2]);
    numberName.push('hundred');
    n -= d2 * 100;
  }

  let d10 = n;
  if (d10 != 0) {
    numberName.push(names100[d10]);
  }

  return numberName.join(' ');
}

export function registerQueriesBaseTests() {
  beforeEach(async () => {
    try {
      if (db) {
        db.close();
        db.delete();
      }

      global.db = db = open('test', {
        numReadConnections: 0,
      });

      await db.execute('DROP TABLE IF EXISTS t1;');
      await db.execute(
        'CREATE TABLE IF NOT EXISTS t1(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER, c TEXT);'
      );
      await db.execute(
        'CREATE TABLE IF NOT EXISTS t2(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER, c TEXT)'
      );
    } catch (e) {
      console.warn('error on before each', e);
    }
  });

  describe('Queries tests', () => {
    it('1000 INSERTs', async () => {
      let res: QueryResult;
      let start = performance.now();
      for (let i = 0; i < 1000; i++) {
        const n = randomIntFromInterval(0, 100000);
        res = await db.execute('INSERT INTO t1(a, b, c) VALUES(?, ?, ?)', [
          0 + i,
          n,
          numberName(n),
        ]);
      }
      let end = performance.now();
      console.log(`Duration: ${(end - start).toFixed(2)}`);
      //   const id = chance.integer();
      //   const name = chance.name();
      //   const age = chance.integer();
      //   const networth = chance.floating();
      //   const res = db.execute(
      //     'INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)',
      //     [id, name, age, networth]
      //   );

      expect(res.rowsAffected).to.equal(1);
      //   expect(res.insertId).to.equal(1);
      //   expect(res.metadata).to.eql([]);
      //   expect(res.rows?._array).to.eql([]);
      //   expect(res.rows?.length).to.equal(0);
      //   expect(res.rows?.item).to.be.a('function');
    });

    it('25000 INSERTs', async () => {
      let res: QueryResult;
      let start = performance.now();
      await db.writeTransaction(async (tx) => {
        for (let i = 0; i < 25000; ++i) {
          const n = randomIntFromInterval(0, 100000);
          await tx.execute(`INSERT INTO t2(a, b, c) VALUES(?, ?, ?)`, [
            i + 1,
            n,
            numberName(n),
          ]);
        }
      });
      await db.execute('PRAGMA wal_checkpoint(RESTART)');
      let end = performance.now();
      let duration = end - start;
      console.log(`25000 INSERTs :: ${duration}ms`);
    });
  });
}
