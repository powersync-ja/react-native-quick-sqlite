import { RowUpdateType, UpdateCallback } from './types';

const updateCallbacks: Record<string, UpdateCallback> = {};

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
