import Chance from 'chance';
import { open, QuickSQLiteConnection, SQLBatchTuple, UpdateNotification } from 'react-native-quick-sqlite';
import { beforeEach, describe, it } from '../mocha/MochaRNAdapter';
import chai from 'chai';

let expect = chai.expect;
const chance = new Chance();

// Need to store the db on the global state since this variable will be cleared on hot reload,
// Attempting to open an already open DB results in an error.
let db: QuickSQLiteConnection = global.db;

function generateUserInfo() {
  return { id: chance.integer(), name: chance.name(), age: chance.integer(), networth: chance.floating() };
}

export function registerBaseTests() {
  beforeEach(async () => {
    try {
      if (db) {
        db.close();
        db.delete();
      }

      global.db = db = open('test');

      await db.execute('DROP TABLE IF EXISTS User; ');
      await db.execute('CREATE TABLE User ( id INT PRIMARY KEY, name TEXT NOT NULL, age INT, networth REAL) STRICT;');
    } catch (e) {
      console.warn('error on before each', e);
    }
  });

  describe('Raw queries', () => {
    it('Insert what', async () => {
      const { id, name, age, networth } = generateUserInfo();
      const res = await db.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
        id,
        name,
        age,
        networth
      ]);

      expect(res.rowsAffected).to.equal(1);
      expect(res.insertId).to.equal(1);
      expect(res.metadata).to.eql([]);
      expect(res.rows?._array).to.eql([]);
      expect(res.rows?.length).to.equal(0);
      expect(res.rows?.item).to.be.a('function');
    });

    it('Query without params', async () => {
      const { id, name, age, networth } = generateUserInfo();
      db.execute('INSERT INTO User (id, name, age, networth) VALUES(?, ?, ?, ?)', [id, name, age, networth]);

      const res = await db.execute('SELECT * FROM User');

      expect(res.rowsAffected).to.equal(1);
      expect(res.insertId).to.equal(1);
      expect(res.rows?._array).to.eql([
        {
          id,
          name,
          age,
          networth
        }
      ]);
    });

    it('Query with params', async () => {
      const { id, name, age, networth } = generateUserInfo();
      await db.execute('INSERT INTO User (id, name, age, networth) VALUES(?, ?, ?, ?)', [id, name, age, networth]);

      const res = await db.execute('SELECT * FROM User WHERE id = ?', [id]);

      expect(res.rowsAffected).to.equal(1);
      expect(res.insertId).to.equal(1);
      expect(res.rows?._array).to.eql([
        {
          id,
          name,
          age,
          networth
        }
      ]);
    });

    it('Failed insert', async () => {
      const { id, name, age, networth } = generateUserInfo();
      // expect(
      try {
        await db.execute('INSERT INTO User (id, name, age, networth) VALUES(?, ?, ?, ?)', [id, name, age, networth]);
      } catch (e: any) {
        expect(typeof e).to.equal('object');

        expect(e.message).to.include(`cannot store TEXT value in INT column User.id`);
      }
    });

    it('Transaction, auto commit', async () => {
      const { id, name, age, networth } = generateUserInfo();
      await db.writeTransaction(async (tx) => {
        const res = tx.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
          id,
          name,
          age,
          networth
        ]);

        expect(res.rowsAffected).to.equal(1);
        expect(res.insertId).to.equal(1);
        expect(res.metadata).to.eql([]);
        expect(res.rows?._array).to.eql([]);
        expect(res.rows?.length).to.equal(0);
        expect(res.rows?.item).to.be.a('function');
      });

      const res = await db.execute('SELECT * FROM User');
      expect(res.rows?._array).to.eql([
        {
          id,
          name,
          age,
          networth
        }
      ]);
    });

    it('Transaction, manual commit', async () => {
      const { id, name, age, networth } = generateUserInfo();
      await db.writeTransaction(async (tx) => {
        const res = tx.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
          id,
          name,
          age,
          networth
        ]);

        expect(res.rowsAffected).to.equal(1);
        expect(res.insertId).to.equal(1);
        expect(res.metadata).to.eql([]);
        expect(res.rows?._array).to.eql([]);
        expect(res.rows?.length).to.equal(0);
        expect(res.rows?.item).to.be.a('function');

        tx.commit();
      });

      const res = await db.execute('SELECT * FROM User');
      expect(res.rows?._array).to.eql([
        {
          id,
          name,
          age,
          networth
        }
      ]);
    });

    it('Transaction, executed in order', async () => {
      // ARRANGE: Setup for multiple transactions
      const iterations = 10;
      const actual: unknown[] = [];

      // ARRANGE: Generate expected data
      const id = chance.integer();
      const name = chance.name();
      const age = chance.integer();

      // ACT: Start multiple transactions to upsert and select the same record
      const promises = [];
      for (let iteration = 1; iteration <= iterations; iteration++) {
        const promised = db.writeTransaction(async (tx) => {
          // ACT: Upsert statement to create record / increment the value
          tx.execute(
            `
              INSERT OR REPLACE INTO [User] ([id], [name], [age], [networth])
              SELECT ?, ?, ?,
                IFNULL((
                  SELECT [networth] + 1000
                  FROM [User]
                  WHERE [id] = ?
                ), 0)
          `,
            [id, name, age, id]
          );

          // ACT: Select statement to get incremented value and store it for checking later
          const results = tx.execute('SELECT [networth] FROM [User] WHERE [id] = ?', [id]);

          actual.push(results.rows?._array[0].networth);
        });

        promises.push(promised);
      }

      // ACT: Wait for all transactions to complete
      await Promise.all(promises);

      // ASSERT: That the expected values where returned
      const expected = Array(iterations)
        .fill(0)
        .map((_, index) => index * 1000);
      expect(actual).to.eql(expected, 'Each transaction should read a different value');
    });

    it('Transaction, cannot execute after commit', async () => {
      const { id, name, age, networth } = generateUserInfo();
      await db.writeTransaction(async (tx) => {
        const res = tx.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
          id,
          name,
          age,
          networth
        ]);

        expect(res.rowsAffected).to.equal(1);
        expect(res.insertId).to.equal(1);
        expect(res.metadata).to.eql([]);
        expect(res.rows?._array).to.eql([]);
        expect(res.rows?.length).to.equal(0);
        expect(res.rows?.item).to.be.a('function');

        tx.commit();

        let errorThrown = false;
        try {
          tx.execute('SELECT * FROM "User"');
        } catch (e) {
          errorThrown = true;
          expect(!!e).to.equal(true);
        }

        expect(errorThrown).to.equal(true);
      });

      const res = await db.execute('SELECT * FROM User ');
      expect(res.rows?._array).to.eql([
        {
          id,
          name,
          age,
          networth
        }
      ]);
    });

    it('Incorrect transaction, manual rollback ', async () => {
      const id = chance.string;
      const { name, age, networth } = generateUserInfo();
      await db.writeTransaction(async (tx) => {
        try {
          tx.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [id, name, age, networth]);
        } catch (e) {
          tx.rollback();
        }
      });

      const res = await db.execute('SELECT * FROM User');
      expect(res.rows?._array).to.eql([]);
    });

    it('Correctly throws', async () => {
      const id = chance.string();
      const { name, age, networth } = generateUserInfo();
      try {
        await db.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [id, name, age, networth]);
      } catch (e: any) {
        expect(!!e).to.equal(true);
      }
    });

    it('Rollback', async () => {
      const { id, name, age, networth } = generateUserInfo();

      await db.writeTransaction(async (tx) => {
        tx.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [id, name, age, networth]);
        tx.rollback();
      });

      const res = await db.execute('SELECT * FROM User');
      expect(res.rows?._array).to.eql([]);
    });

    it('Transaction, rejects on callback error', async () => {
      const promised = db.writeTransaction(async (tx) => {
        throw new Error('Error from callback');
      });

      // ASSERT: should return a promise that eventually rejects
      expect(promised).to.have.property('then').that.is.a('function');
      try {
        await promised;
        expect.fail('Should not resolve');
      } catch (e) {
        expect(e).to.be.a.instanceof(Error);
        expect((e as Error)?.message).to.equal('Error from callback');
      }
    });

    it('Transaction, rejects on invalid query', async () => {
      const promised = db.writeTransaction(async (tx) => {
        tx.execute('SELECT * FROM [tableThatDoesNotExist];');
      });

      // ASSERT: should return a promise that eventually rejects
      expect(promised).to.have.property('then').that.is.a('function');
      try {
        await promised;
        expect.fail('Should not resolve');
      } catch (e) {
        expect(e).to.be.a.instanceof(Error);
        expect((e as Error)?.message).to.include('no such table: tableThatDoesNotExist');
      }
    });

    it('Transaction, handle async callback', async () => {
      let ranCallback = false;
      const promised = db.writeTransaction(async (tx) => {
        await new Promise<void>((done) => {
          setTimeout(() => done(), 50);
        });
        tx.execute('SELECT * FROM [User];');
        ranCallback = true;
      });

      // ASSERT: should return a promise that eventually rejects
      expect(promised).to.have.property('then').that.is.a('function');
      await promised;
      expect(ranCallback).to.equal(true, 'Should handle async callback');
    });

    it('Async transaction, auto commit', async () => {
      const { id, name, age, networth } = generateUserInfo();

      await db.writeTransaction(async (tx) => {
        const res = await tx.executeAsync('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
          id,
          name,
          age,
          networth
        ]);

        expect(res.rowsAffected).to.equal(1);
        expect(res.insertId).to.equal(1);
        expect(res.metadata).to.eql([]);
        expect(res.rows?._array).to.eql([]);
        expect(res.rows?.length).to.equal(0);
        expect(res.rows?.item).to.be.a('function');
      });

      const res = await db.execute('SELECT * FROM User');
      expect(res.rows?._array).to.eql([
        {
          id,
          name,
          age,
          networth
        }
      ]);
    });

    it('Async transaction, auto rollback', async () => {
      const id = chance.string(); // Causes error because it should be an integer
      const { name, age, networth } = generateUserInfo();

      try {
        await db.writeTransaction(async (tx) => {
          await tx.executeAsync('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
            id,
            name,
            age,
            networth
          ]);
        });
      } catch (error) {
        expect(error).to.be.instanceOf(Error);
        expect((error as Error).message)
          .to.include('SQL execution error')
          .and.to.include('cannot store TEXT value in INT column User.id');

        const res = await db.execute('SELECT * FROM User');
        expect(res.rows?._array).to.eql([]);
      }
    });

    it('Async transaction, manual commit', async () => {
      const { id, name, age, networth } = generateUserInfo();

      await db.writeTransaction(async (tx) => {
        await tx.executeAsync('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
          id,
          name,
          age,
          networth
        ]);
        tx.commit();
      });

      const res = await db.execute('SELECT * FROM User');
      expect(res.rows?._array).to.eql([
        {
          id,
          name,
          age,
          networth
        }
      ]);
    });

    it('Async transaction, manual rollback', async () => {
      const { id, name, age, networth } = generateUserInfo();

      await db.writeTransaction(async (tx) => {
        await tx.executeAsync('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
          id,
          name,
          age,
          networth
        ]);
        tx.rollback();
      });

      const res = await db.execute('SELECT * FROM User');
      expect(res.rows?._array).to.eql([]);
    });

    it('Async transaction, executed in order', async () => {
      // ARRANGE: Setup for multiple transactions
      const iterations = 10;
      const actual: unknown[] = [];

      // ARRANGE: Generate expected data
      const id = chance.integer();
      const name = chance.name();
      const age = chance.integer();

      // ACT: Start multiple async transactions to upsert and select the same record
      const promises = [];
      for (let iteration = 1; iteration <= iterations; iteration++) {
        const promised = db.writeTransaction(async (tx) => {
          // ACT: Upsert statement to create record / increment the value
          await tx.executeAsync(
            `
              INSERT OR REPLACE INTO [User] ([id], [name], [age], [networth])
              SELECT ?, ?, ?,
                IFNULL((
                  SELECT [networth] + 1000
                  FROM [User]
                  WHERE [id] = ?
                ), 0)
          `,
            [id, name, age, id]
          );

          // ACT: Select statement to get incremented value and store it for checking later
          const results = await tx.executeAsync('SELECT [networth] FROM [User] WHERE [id] = ?', [id]);

          actual.push(results.rows?._array[0].networth);
        });

        promises.push(promised);
      }

      // ACT: Wait for all transactions to complete
      await Promise.all(promises);

      // ASSERT: That the expected values where returned
      const expected = Array(iterations)
        .fill(0)
        .map((_, index) => index * 1000);
      expect(actual).to.eql(expected, 'Each transaction should read a different value');
    });

    it('Async transaction, rejects on callback error', async () => {
      const promised = db.writeTransaction(async (tx) => {
        throw new Error('Error from callback');
      });

      // ASSERT: should return a promise that eventually rejects
      expect(promised).to.have.property('then').that.is.a('function');
      try {
        await promised;
        expect.fail('Should not resolve');
      } catch (e) {
        expect(e).to.be.a.instanceof(Error);
        expect((e as Error)?.message).to.equal('Error from callback');
      }
    });

    it('Async transaction, rejects on invalid query', async () => {
      const promised = db.writeTransaction(async (tx) => {
        await tx.executeAsync('SELECT * FROM [tableThatDoesNotExist];');
      });

      // ASSERT: should return a promise that eventually rejects
      expect(promised).to.have.property('then').that.is.a('function');
      try {
        await promised;
        expect.fail('Should not resolve');
      } catch (e) {
        expect(e).to.be.a.instanceof(Error);
        expect((e as Error)?.message).to.include('no such table: tableThatDoesNotExist');
      }
    });

    it('Async batch execute', async () => {
      const { id: id1, name: name1, age: age1, networth: networth1 } = generateUserInfo();
      const { id: id2, name: name2, age: age2, networth: networth2 } = generateUserInfo();

      const commands: SQLBatchTuple[] = [
        ['INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [id1, name1, age1, networth1]],
        ['INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [id2, name2, age2, networth2]]
      ];

      await db.executeBatchAsync(commands);

      const res = await db.execute('SELECT * FROM User');
      expect(res.rows?._array).to.eql([
        { id: id1, name: name1, age: age1, networth: networth1 },
        {
          id: id2,
          name: name2,
          age: age2,
          networth: networth2
        }
      ]);
    });

    it('Read lock should be read only', async () => {
      const { id, name, age, networth } = generateUserInfo();

      try {
        await db.readLock(async (context) => {
          await context.executeAsync('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [
            id,
            name,
            age,
            networth
          ]);
        });
        throw new Error('Did not throw');
      } catch (ex) {
        expect(ex.message).to.include('attempt to write a readonly database');
      }
    });

    it('Read locks should queue if exceed number of connections', async () => {
      const { id, name, age, networth } = generateUserInfo();

      await db.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [id, name, age, networth]);

      const numberOfReads = 20;
      const lockResults = await Promise.all(
        new Array(numberOfReads)
          .fill(0)
          .map(() => db.readLock((context) => context.executeAsync('SELECT * FROM USER WHERE name = ?', [name])))
      );

      expect(lockResults.map((r) => r.rows?.item(0).name)).to.deep.equal(new Array(numberOfReads).fill(name));
    });

    it('Multiple reads should occur at the same time', async () => {
      const messages: string[] = [];

      let lock1Resolve: () => void | null = null;
      const lock1Promise = new Promise<void>((resolve) => {
        lock1Resolve = resolve;
      });

      // This wont resolve or free until another connection free's it
      const p1 = db.readLock(async (context) => {
        await lock1Promise;
        messages.push('At the end of 1');
      });

      let lock2Resolve: () => void | null = null;
      const lock2Promise = new Promise<void>((resolve) => {
        lock2Resolve = resolve;
      });
      const p2 = db.readLock(async (context) => {
        // If we get here, then we have a lock open (even though above is locked)
        await lock2Promise;
        lock1Resolve();
        messages.push('At the end of 2');
      });

      const p3 = db.readLock(async (context) => {
        lock2Resolve();
        messages.push('At the end of 3');
      });

      await Promise.all([p1, p2, p3]);

      expect(messages).deep.equal(['At the end of 3', 'At the end of 2', 'At the end of 1']);
    });

    it('Should be able to read while a write is running', async () => {
      let lock1Resolve: () => void | null = null;
      const lock1Promise = new Promise<void>((resolve) => {
        lock1Resolve = resolve;
      });

      // This wont resolve or free until another connection free's it
      db.writeLock(async (context) => {
        await lock1Promise;
      });

      const result = await db.readLock(async (context) => {
        // Read logic could execute here while writeLock is still open
        lock1Resolve();
        return 42;
      });

      expect(result).to.equal(42);
    });

    it('Should call update hook on changes', async () => {
      const result = new Promise<UpdateNotification>((resolve) => db.registerUpdateHook((update) => resolve(update)));

      const { id, name, age, networth } = generateUserInfo();

      await db.execute('INSERT INTO "User" (id, name, age, networth) VALUES(?, ?, ?, ?)', [id, name, age, networth]);

      const update = await result;

      expect(update.table).to.equal('User');
    });

    it('Should open a db without concurrency', async () => {
      const singleConnection = open('single_connection', { numReadConnections: 0 });

      const [p1, p2] = [
        singleConnection.writeLock(async () => {
          await new Promise((resolve) => setTimeout(resolve, 200));
        }),
        // Expect an exception and return it
        singleConnection.readLock(async () => {}, { timeoutMs: 100 }).catch((ex) => ex)
      ];

      expect(await p1).to.equal(undefined);

      expect((await p2).message).to.include('timed out');

      singleConnection.close();
    });
  });
}