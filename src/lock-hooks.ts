/**
 * Hooks which can be triggered during the execution of read/write locks
 */
export interface LockHooks {
  /**
   * Executed after a SQL statement has been executed
   */
  execute?: (sql: string, args?: any[]) => Promise<void>;
  lockAcquired?: () => Promise<void>;
  lockReleased?: () => Promise<void>;
}

export interface TransactionHooks extends LockHooks {
  begin?: () => Promise<void>;
  commit?: () => Promise<void>;
  rollback?: () => Promise<void>;
}
