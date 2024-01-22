import _ from 'lodash';
import { registerUpdateHook } from './table-updates';
import { UpdateCallback, UpdateNotification } from './types';
import { BaseListener, BaseObserver } from './utils/BaseObserver';

export interface DBListenerManagerOptions {
  dbName: string;
}

export enum WriteTransactionEventType {
  STARTED = 'started',
  COMMIT = 'commit',
  ROLLBACK = 'rollback'
}

export interface WriteTransactionEvent {
  type: WriteTransactionEventType;
}

export interface DBListener extends BaseListener {
  /**
   * Register a listener to be fired for any table change.
   * Changes inside write transactions are reported immediately.
   */
  rawTableChange: UpdateCallback;

  /**
   * Register a listener for when table changes are persisted
   * into the DB. Changes during write transactions which are
   * rolled back are not reported.
   * Any changes during write transactions are buffered and reported
   * after commit.
   * Table changes are reported individually for now in order to maintain
   * API compatibility. These can be batched in future.
   */
  tableUpdated: UpdateCallback;

  /**
   * Listener event triggered whenever a write transaction
   * is started, committed or rolled back.
   */
  writeTransaction: (event: WriteTransactionEvent) => void;
}

export class DBListenerManager extends BaseObserver<DBListener> {}

export class DBListenerManagerInternal extends DBListenerManager {
  private _writeTransactionActive: boolean;
  private updateBuffer: UpdateNotification[];

  get writeTransactionActive() {
    return this._writeTransactionActive;
  }

  constructor(protected options: DBListenerManagerOptions) {
    super();
    this._writeTransactionActive = false;
    this.updateBuffer = [];
    registerUpdateHook(this.options.dbName, (update) => this.handleTableUpdates(update));
  }

  transactionStarted() {
    this._writeTransactionActive = true;
    this.iterateListeners((l) => l?.writeTransaction?.({ type: WriteTransactionEventType.STARTED }));
  }

  transactionCommitted() {
    this._writeTransactionActive = false;
    // flush updates
    const uniqueUpdates = _.uniq(this.updateBuffer);
    this.updateBuffer = [];
    this.iterateListeners((l) => {
      l.writeTransaction?.({ type: WriteTransactionEventType.COMMIT });
      uniqueUpdates.forEach((update) => l.tableUpdated?.(update));
    });
  }

  transactionReverted() {
    this._writeTransactionActive = false;
    // clear updates
    this.updateBuffer = [];
    this.iterateListeners((l) => l?.writeTransaction?.({ type: WriteTransactionEventType.ROLLBACK }));
  }

  handleTableUpdates(notification: UpdateNotification) {
    // Fire updates for any change
    this.iterateListeners((l) => l.rawTableChange?.({ ...notification, pendingCommit: this._writeTransactionActive }));

    if (this.writeTransactionActive) {
      this.updateBuffer.push(notification);
      return;
    }

    this.iterateListeners((l) => l.tableUpdated?.(notification));
  }
}
