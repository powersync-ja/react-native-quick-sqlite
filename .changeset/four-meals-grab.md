---
'@journeyapps/react-native-quick-sqlite': patch
---

This pull request improves the performance of releasing lock operations. Executing multiple lock operations, such as individual calls to `.execute`, should see a significant performance improvement.
