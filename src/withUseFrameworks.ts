const { ConfigPlugin, withDangerousMod, createRunOncePlugin } = require('@expo/config-plugins');
const fs = require('fs');
const path = require('path');

// Define package metadata
const pkg = { name: '@journeyapps/react-native-quick-sqlite', version: 'UNVERSIONED' };

// Function to modify the Podfile
function modifyPodfile(podfilePath: string, staticLibrary: boolean) {
  if (!staticLibrary) {
    return;
  }

  let podfile = fs.readFileSync(podfilePath, 'utf8');
  const preinstallScript = `
pre_install do |installer|
  installer.pod_targets.each do |pod|
    if pod.name.eql?('react-native-quick-sqlite')
      def pod.build_type
        Pod::BuildType.static_library
      end
    end
  end
end
`;
  // Ensure script is added only once
  if (!podfile.includes('react-native-quick-sqlite')) {
    podfile = podfile.replace(/target\s+'[^']+'\s+do/, `$&\n${preinstallScript}`);
    fs.writeFileSync(podfilePath, podfile, 'utf8');
    console.log(`Added pre_install script for react-native-quick-sqlite to Podfile`);
  }
}

// Config Plugin
const withUseFrameworks = (config, options = { staticLibrary: false }) => {
  const { staticLibrary } = options;

  return withDangerousMod(config, [
    'ios',
    (config) => {
      const podfilePath = path.join(config.modRequest.platformProjectRoot, 'Podfile');
      if (fs.existsSync(podfilePath)) {
        modifyPodfile(podfilePath, staticLibrary);
      } else {
        console.warn(`Podfile not found at ${podfilePath}`);
      }
      return config;
    }
  ]);
};

const pluginWithOptions = (config, options) => {
  return withUseFrameworks(config, options);
};

// Export the plugin with Expo's createRunOncePlugin
module.exports = createRunOncePlugin(pluginWithOptions, pkg.name, pkg.version);
