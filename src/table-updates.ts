import { RowUpdateType, TransactionCallback, TransactionEvent, UpdateCallback } from './types';

const updateCallbacks: Record<string, UpdateCallback> = {};
const transactionCallbacks: Record<string, TransactionCallback> = {};

/**
 * Entry point for update callbacks. This is triggered from C++ with params.
 */
global.triggerUpdateHook = function (dbName: string, table: string, opType: RowUpdateType, rowId: number) {
  const callback = updateCallbacks[dbName];
  if (!callback) {
    return;
  }

  callback({
    opType,
    table,
    rowId
  });
  return null;
};

export const registerUpdateHook = (dbName: string, callback: UpdateCallback) => {
  updateCallbacks[dbName] = callback;
};

/**
 * Entry point for transaction callbacks. This is triggered from C++ with params.
 */
global.triggerTransactionFinalizerHook = function (dbName: string, eventType: TransactionEvent) {
  const callback = transactionCallbacks[dbName];
  if (!callback) {
    return;
  }

  callback(eventType);
  return null;
};

export const registerTransactionHook = (dbName: string, callback: TransactionCallback) => {
  transactionCallbacks[dbName] = callback;
};
