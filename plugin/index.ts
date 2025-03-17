const { withDangerousMod } = require('@expo/config-plugins');
const fs = require('fs');
const path = require('path');

function modifyPodfile(podfilePath) {
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

  // Ensure we don't add duplicate scripts
  if (!podfile.includes('react-native-quick-sqlite')) {
    podfile = podfile.replace(/target\s+'[^']+'\s+do/, `$&\n${preinstallScript}`);
    fs.writeFileSync(podfilePath, podfile, 'utf8');
  }
}

const withPowerSyncQuickSQLite = (config) => {
  return withDangerousMod(config, [
    'ios',
    (config) => {
      const podfilePath = path.join(config.modRequest.platformProjectRoot, 'Podfile');
      modifyPodfile(podfilePath);
      return config;
    },
  ]);
};

module.exports = withPowerSyncQuickSQLite;
