---
'@journeyapps/react-native-quick-sqlite': patch
---

Fixed: Missing dependency for `uuid` and race condition where ommitting `await` on some lock/transaction operations could deadlock the application's main thread.
