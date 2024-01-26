/**
 * Hooks which can be triggered during the execution of read/write locks
 */
export interface LockHooks {
  lockAcquired?: () => Promise<void>;
  lockReleased?: () => Promise<void>;
}
