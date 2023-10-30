import { DataSource } from 'typeorm';
import { typeORMDriver } from 'react-native-quick-sqlite';
import { Book } from './models/Book';
import { User } from './models/User';
let datasource: DataSource;

export async function typeORMInit() {
  datasource = new DataSource({
    type: 'react-native',
    database: 'typeormdb',
    location: '.',
    driver: typeORMDriver,
    entities: [Book, User],
    synchronize: true
  });

  await datasource.initialize();

  const bookRepository = datasource.getRepository(Book);
  const book1 = new Book();
  book1.id = Math.random().toString();
  book1.title = 'Lord of the rings';

  await bookRepository.save(book1);
}

export async function typeORMGetBooks() {
  const bookRepository = datasource.getRepository(Book);
  return await bookRepository.find();
}

export async function executeFailingTypeORMQuery() {
  const bookRepository = datasource.getRepository(Book);

  try {
    const manualQuery = await bookRepository.query(`
          SELECT * From UnexistingTable
        `);
  } catch (e) {
    console.warn('should have cached');
  }
}
