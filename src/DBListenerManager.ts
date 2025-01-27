import { registerTransactionHook, registerUpdateHook } from './table-updates';
import {
  BatchedUpdateCallback,
  BatchedUpdateNotification,
  TransactionEvent,
  UpdateCallback,
  UpdateNotification
} from './types';
import { BaseListener, BaseObserver } from './utils/BaseObserver';

export interface DBListenerManagerOptions {
  dbName: string;
}

export interface WriteTransactionEvent {
  type: TransactionEvent;
}

export interface DBListener extends BaseListener {
  /**
   * Register a listener to be fired for any table change.
   * Changes inside write locks and transactions are reported immediately.
   */
  rawTableChange: UpdateCallback;

  /**
   * Register a listener for when table changes are persisted
   * into the DB. Changes during write transactions which are
   * rolled back are not reported.
   * Any changes during write locks are buffered and reported
   * after transaction commit and lock release.
   * Table changes are reported individually for now in order to maintain
   * API compatibility. These can be batched in future.
   */
  tablesUpdated: BatchedUpdateCallback;

  /**
   * Listener event triggered whenever a write transaction
   * is started, committed or rolled back.
   */
  writeTransaction: (event: WriteTransactionEvent) => void;

  closed: () => void;
}

export class DBListenerManager extends BaseObserver<DBListener> {}

export class DBListenerManagerInternal extends DBListenerManager {
  private updateBuffer: UpdateNotification[];

  constructor(protected options: DBListenerManagerOptions) {
    super();
    this.updateBuffer = [];
    registerUpdateHook(this.options.dbName, (update) => this.handleTableUpdates(update));
    registerTransactionHook(this.options.dbName, (eventType) => {
      switch (eventType) {
        /**
         * COMMIT hooks occur before the commit is completed. This leads to race conditions.
         * Only use the rollback event to clear updates.
         */
        case TransactionEvent.ROLLBACK:
          this.transactionReverted();
          break;
      }

      this.iterateListeners((l) =>
        l.writeTransaction?.({
          type: eventType
        })
      );
    });
  }

  flushUpdates() {
    if (!this.updateBuffer.length) {
      return;
    }

    const groupedUpdates = this.updateBuffer.reduce((grouping: Record<string, UpdateNotification[]>, update) => {
      const { table } = update;
      const updateGroup = grouping[table] || (grouping[table] = []);
      updateGroup.push(update);
      return grouping;
    }, {});

    const batchedUpdate: BatchedUpdateNotification = {
      groupedUpdates,
      rawUpdates: this.updateBuffer,
      tables: Object.keys(groupedUpdates)
    };
    this.updateBuffer = [];
    this.iterateListeners((l) => l.tablesUpdated?.(batchedUpdate));
  }

  protected transactionReverted() {
    // clear updates
    this.updateBuffer = [];
  }

  handleTableUpdates(notification: UpdateNotification) {
    // Fire updates for any change
    this.iterateListeners((l) => l.rawTableChange?.({ ...notification }));

    // Queue changes until they are flushed
    this.updateBuffer.push(notification);
  }
}
