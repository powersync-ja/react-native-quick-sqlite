---
'@journeyapps/react-native-quick-sqlite': minor
---

Added `registerTablesChangedHook` to DB connections which reports batched table updates once `writeTransaction`s and `writeLock`s have been committed. Maintained API compatibility with `registerUpdateHook` which reports table change events as they occur. Added listeners for when write transactions have been committed or rolled back.
