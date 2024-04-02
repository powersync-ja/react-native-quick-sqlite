# @journeyapps/react-native-quick-sqlite

## 1.1.3

### Patch Changes

- b12ec4d: This pull request improves the performance of releasing lock operations. Executing multiple lock operations, such as individual calls to `.execute`, should see a significant performance improvement.

## 1.1.2

### Patch Changes

- 4979882: Fixed incorrect imports of `sqlite3.h` to use local version.

## 1.1.1

### Patch Changes

- b1324f1: Updated PowerSync SQLite Core to ~>0.1.6. https://github.com/powersync-ja/powersync-sqlite-core/pull/3

## 1.1.0

### Minor Changes

- 3bb0212: Added `registerTablesChangedHook` to DB connections which reports batched table updates once `writeTransaction`s and `writeLock`s have been committed. Maintained API compatibility with `registerUpdateHook` which reports table change events as they occur. Added listeners for when write transactions have been committed or rolled back.

## 1.0.0

### Major Changes

- 7c54e8a: Version 1.0.0 release out of beta

## 0.1.1

### Patch Changes

- 2802916: Fixed: Missing dependency for `uuid` and race condition where ommitting `await` on some lock/transaction operations could deadlock the application's main thread.

## 0.1.0

### Minor Changes

- 21cdcf1: Bump to beta version

## 0.0.2

### Patch Changes

- c17f91c: Added concurrent read/write connection support.

## 0.0.1

### Patch Changes

- 90affb4: Bump version to 0.0.1 for consistency
