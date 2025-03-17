<p align="center">
  <a href="https://www.powersync.com" target="_blank"><img src="https://github.com/powersync-ja/react-native-quick-sqlite/assets/19345049/40e62305-1089-4277-a6ac-dfc18934c114"/></a>
</p>

This repository is a fork of [react-native-quick-sqlite](https://github.com/ospfranco/react-native-quick-sqlite?tab=readme-ov-file) that includes custom SQLite extensions built specifically for **PowerSync**. It has been modified to meet the needs of **PowerSync**, adding features or behaviors that are different from the original repository.

It is **not** intended to be used independently, use [PowerSync React Native SDK](https://github.com/powersync-ja/powersync-js/tree/main/packages/react-native) and install this alongside it as a peer dependency.

### For Expo

#### Using `use_frameworks!` on iOS? Add the Config Plugin

If your iOS project uses `use_frameworks!`, you need to add the config plugin to your **app.json** or **app.config.js** to ensure proper setup.

#### Steps to Add the Plugin

#### **For `app.json` (Recommended for most users):**

Add the following snippet inside the `"expo"` section:

```json
{
  "expo": {
    "plugins": [["@journeyapps/react-native-quick-sqlite"]]
  }
}
```
