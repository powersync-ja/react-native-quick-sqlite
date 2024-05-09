module.exports = function (api) {
  api.cache(true);
  return {
    presets: ['babel-preset-expo'],
    plugins: [
      [
        'module-resolver',
        {
          alias: {
            stream: 'stream-browserify',
            'react-native-sqlite-storage': 'react-native-quick-sqlite'
          }
        }
      ]
    ]
  };
};
