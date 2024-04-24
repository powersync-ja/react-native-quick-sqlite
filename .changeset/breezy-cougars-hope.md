---
'@journeyapps/react-native-quick-sqlite': patch
---

Fix race condition where table change notications would trigger before COMMIT had completed.
