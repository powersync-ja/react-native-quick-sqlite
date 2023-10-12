/**
 * Object returned by SQL Query executions {
 *  insertId: Represent the auto-generated row id if applicable
 *  rowsAffected: Number of affected rows if result of a update query
 *  message: if status === 1, here you will find error description
 *  rows: if status is undefined or 0 this object will contain the query results
 * }
 *
 * @interface QueryResult
 */
export type QueryResult = {
  insertId?: number;
  rowsAffected: number;
  rows?: {
    /** Raw array with all dataset */
    _array: any[];
    /** The lengh of the dataset */
    length: number;
    /** A convenience function to acess the index based the row object
     * @param idx the row index
     * @returns the row structure identified by column names
     */
    item: (idx: number) => any;
  };
  /**
   * Query metadata, avaliable only for select query results
   */
  metadata?: ColumnMetadata[];
};

/**
 * Column metadata
 * Describes some information about columns fetched by the query
 */
export type ColumnMetadata = {
  /** The name used for this column for this resultset */
  columnName: string;
  /** The declared column type for this column, when fetched directly from a table or a View resulting from a table column. "UNKNOWN" for dynamic values, like function returned ones. */
  columnDeclaredType: string;
  /**
   * The index for this column for this resultset*/
  columnIndex: number;
};

/**
 * Allows the execution of bulk of sql commands
 * inside a transaction
 * If a single query must be executed many times with different arguments, its preferred
 * to declare it a single time, and use an array of array parameters.
 */
export type SQLBatchTuple = [string] | [string, Array<any> | Array<Array<any>>];

/**
 * status: 0 or undefined for correct execution, 1 for error
 * message: if status === 1, here you will find error description
 * rowsAffected: Number of affected rows if status == 0
 */
export type BatchQueryResult = {
  rowsAffected?: number;
};

/**
 * Result of loading a file and executing every line as a SQL command
 * Similar to BatchQueryResult
 */
export interface FileLoadResult extends BatchQueryResult {
  commands?: number;
}

export interface Transaction {
  // TODO give blocking access in order to use a single thread
  commit: () => Promise<QueryResult>;
  commitAsync: () => Promise<QueryResult>;
  // TODO give blocking access in order to use a single thread
  execute: (query: string, params?: any[]) => QueryResult;
  executeAsync: (query: string, params?: any[] | undefined) => Promise<QueryResult>;
  // TODO give blocking access in order to use a single thread
  rollback: () => QueryResult;
  rollbackAsync: () => Promise<QueryResult>;
}

export enum RowUpdateType {
  SQLITE_INSERT = 18,
  SQLITE_DELETE = 9,
  SQLITE_UPDATE = 23
}
export interface UpdateNotification {
  opType: RowUpdateType;
  table: string;
  rowId: number;
}

export type UpdateCallback = (update: UpdateNotification) => void;

export type ContextLockID = string;

export enum ConcurrentLockType {
  READ,
  WRITE
}

export interface ISQLite {
  open: (dbName: string, location?: string) => void;
  close: (dbName: string) => void;
  delete: (dbName: string, location?: string) => void;

  executeInContext: (dbName: string, id: ContextLockID, query: string, params: any[]) => Promise<QueryResult>;
  requestConcurrentLock: (dbName: string, id: ContextLockID, type: ConcurrentLockType) => QueryResult;
  releaseConcurrentLock(dbName: string, id: ContextLockID): void;

  // TODO
  attach: (mainDbName: string, dbNameToAttach: string, alias: string, location?: string) => void;
  detach: (mainDbName: string, alias: string) => void;

  // TODO
  executeBatch: (dbName: string, commands: SQLBatchTuple[]) => BatchQueryResult;
  executeBatchAsync: (dbName: string, commands: SQLBatchTuple[]) => Promise<BatchQueryResult>;

  // TODO
  loadFile: (dbName: string, location: string) => FileLoadResult;
  loadFileAsync: (dbName: string, location: string) => Promise<FileLoadResult>;
}

export interface LockOptions {
  timeoutMs?: number;
}

export interface LockContext {
  execute: (sql: string, args?: any[]) => Promise<QueryResult>;
}

export interface TransactionContext extends LockContext {
  commit: () => Promise<QueryResult>;
  rollback: () => Promise<QueryResult>;
}

export type QuickSQLiteConnection = {
  close: () => void;
  execute: (sql: string, args?: any[]) => Promise<QueryResult>;
  readLock: <T>(callback: (context: LockContext) => Promise<T>, options?: LockOptions) => Promise<T>;
  readTransaction: <T>(callback: (context: TransactionContext) => Promise<T>, options?: LockOptions) => Promise<T>;
  writeLock: <T>(callback: (context: LockContext) => Promise<T>, options?: LockOptions) => Promise<T>;
  writeTransaction: <T>(callback: (context: TransactionContext) => Promise<T>, options?: LockOptions) => Promise<T>;
  delete: () => void;
  attach: (dbNameToAttach: string, alias: string, location?: string) => void;
  detach: (alias: string) => void;
  executeBatch: (commands: SQLBatchTuple[]) => BatchQueryResult;
  executeBatchAsync: (commands: SQLBatchTuple[]) => Promise<BatchQueryResult>;
  loadFile: (location: string) => FileLoadResult;
  loadFileAsync: (location: string) => Promise<FileLoadResult>;
  /**
   * Note that only one listener can be registered per database connection.
   * Any new hook registration will override the previous one.
   */
  registerUpdateHook(callback: UpdateCallback): void;
};
