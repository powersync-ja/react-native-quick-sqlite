const path = require('path');
const pak = require('../package.json');
module.exports = function (api) {
  api.cache(true);
  return {
    presets: ['babel-preset-expo'],
    plugins: [
      [
        'module-resolver',
        {
          alias: {
            [pak.name]: path.join(__dirname, '..', pak.source),
            stream: 'stream-browserify',
            'react-native-sqlite-storage': 'react-native-quick-sqlite'
          }
        }
      ]
    ]
  };
};
