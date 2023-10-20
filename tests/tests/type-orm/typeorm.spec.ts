import { DataSource, Repository } from 'typeorm';
import { beforeAll, beforeEach, it, describe } from '../mocha/MochaRNAdapter';
import { typeORMDriver } from 'react-native-quick-sqlite';
import { User } from './models/User';
import { Book } from './models/Book';
import chai from 'chai';
import Chance from 'chance';

const chance = new Chance();
let expect = chai.expect;

let dataSource: DataSource;
let userRepository: Repository<User>;
let bookRepository: Repository<Book>;

export function registerTypeORMTests() {
  describe('Typeorm tests', () => {
    beforeAll((done: any) => {
      dataSource = new DataSource({
        type: 'react-native',
        database: 'typeormDb.sqlite',
        location: 'default',
        driver: typeORMDriver,
        entities: [User, Book],
        synchronize: true
      });

      dataSource
        .initialize()
        .then(() => {
          userRepository = dataSource.getRepository(User);
          bookRepository = dataSource.getRepository(Book);
          done();
        })
        .catch((e) => {
          console.error('error initializing typeORM datasource', e);
          throw e;
        });
    });

    beforeEach(async () => {
      await userRepository.clear();
      await bookRepository.clear();
    });

    it('basic test', async () => {
      const name = 'Steven';
      const user = userRepository.create({
        name,
        age: chance.integer(),
        networth: chance.integer(),
        metadata: { nickname: name },
        avatar: 'Something'
      });
      await userRepository.save(user);

      const found = await userRepository.findOne({ where: { name } });

      expect(found.name).to.equal(name);
    });
  });
}
