import { QueryResult } from './types';

// Add 'item' function to result object to allow the sqlite-storage typeorm driver to work
export const enhanceQueryResult = (result: QueryResult): void => {
  // Add 'item' function to result object to allow the sqlite-storage typeorm driver to work
  if (result.rows == null) {
    result.rows = {
      _array: [],
      length: 0,
      item: (idx: number) => result.rows._array[idx]
    };
  } else {
    result.rows.item = (idx: number) => result.rows._array[idx];
  }
};
