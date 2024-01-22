---
'@journeyapps/react-native-quick-sqlite': minor
---

Fixed table change updates to only trigger change updates for changes made in `writeTransaction` and `writeLock`s which have been commited. Added ability to listen to all table change events as they occur. Added listeners for when a transaction has started, been commited or rolled back.
