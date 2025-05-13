<p align="center">
  <a href="https://www.powersync.com" target="_blank"><img src="https://github.com/powersync-ja/react-native-quick-sqlite/assets/19345049/40e62305-1089-4277-a6ac-dfc18934c114"/></a>
</p>

This repository is a fork of [react-native-quick-sqlite](https://github.com/ospfranco/react-native-quick-sqlite?tab=readme-ov-file) that includes custom SQLite extensions built specifically for **PowerSync**. It has been modified to meet the needs of **PowerSync**, adding features or behaviors that are different from the original repository.

It is **not** intended to be used independently, use [PowerSync React Native SDK](https://github.com/powersync-ja/powersync-js/tree/main/packages/react-native) and install this alongside it as a peer dependency.

### For Expo

#### iOS with `use_frameworks!`

If your iOS project uses `use_frameworks!`, add the `react-native-quick-sqlite` plugin to your **app.json** or **app.config.js** and configure the `staticLibrary` option:

```json
{
  "expo": {
    "plugins": [
      [
        "@journeyapps/react-native-quick-sqlite",
        {
          "staticLibrary": true
        }
      ]
    ]
  }
}
```

This plugin automatically configures the necessary build settings for `react-native-quick-sqlite` to work with `use_frameworks!`.
