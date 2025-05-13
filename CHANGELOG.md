# @journeyapps/react-native-quick-sqlite

## 2.4.4

### Patch Changes

- d743f0c: Update SQLite from 3.39.4 to 3.49.1

## 2.4.3

### Patch Changes

- 9aec2e6: Update PowerSync core extension to version 0.3.14

## 2.4.2

### Patch Changes

- 85b8906: Update core PowerSync extension to 0.3.12

## 2.4.1

### Patch Changes

- 0466bb5: Update PowerSync core extension to 0.3.11

## 2.4.0

### Minor Changes

- 43598db: Improved behaviour for closing a database connection. This should prevent some crash issues.

## 2.3.0

### Minor Changes

- 73a0a68: powersync-sqlite-core 0.3.8: Increase column limit to 1999; improved handling of migrations on views.

## 2.2.1

### Patch Changes

- 761d441: Enable FTS5

## 2.2.0

### Minor Changes

- cbe61de: Upgrade minSdkVersion to 24 for Expo 52

## 2.1.2

### Patch Changes

- a6e21ed: Use powersync-sqlite-core 0.3.6 to fix issue with dangling rows.

## 2.1.1

### Patch Changes

- 9ac4ce7: Result object keys are no longer ordered alphabetically, but rather maintain insertion order. The behaviour now matches other libraries.

## 2.1.0

### Minor Changes

- 23bcb1d: Added `refreshSchema()` to bindings. Will cause all connections to be aware of a schema change.

## 2.0.1

### Patch Changes

- 63cc6b2: Fix issue where new databases would not always have journal_mode = WAL.

## 2.0.0

### Major Changes

- ad95915: Use powersync-sqlite-core 0.3.0

## 1.4.0

### Minor Changes

- 429361a: - Remove use of nativeCallSyncHook() for new architecture support.

## 1.3.1

### Patch Changes

- b8e0524: Use install_modules_dependencies if available in newer react-native versions.

## 1.3.0

### Minor Changes

- 11fc707: Rename back to @journeyapps/react-native-quick-sqlite for now
- 5f70fd2: Use powersync-sqlite-core 0.2.1

## 1.2.0

### Minor Changes

- f9d83cc: Move package to @powersync/react-native-quick-sqlite

## For the following previous versions refer to `@journeyapps/react-native-quick-sqlite`

## 1.1.8

### Patch Changes

- f072d10: Silencing transactions that are reporting on failed rollback exceptions when they are safe to ignore.

## 1.1.7

### Patch Changes

- 421bcbd: The default minimum SDK for Expo 51 is 23, so attempting to compile with our package using 21 would result in a build error.

## 1.1.6

### Patch Changes

- 634c9c2: Removed the requirement for `lodash` and `uuid` packages.

## 1.1.5

### Patch Changes

- 40c6dd0: Fix race condition where table change notications would trigger before COMMIT had completed.
- 2165048: Use memory temp_store

## 1.1.4

### Patch Changes

- a1a7dec: Updated UUID dependency.

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
