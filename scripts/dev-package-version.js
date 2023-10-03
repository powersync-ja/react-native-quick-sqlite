/**
 * Dev package version like 0.0.0-dev-[hash] seem to cause issues with the podspec
 * This script will remove the -dev-hash from the version.
 */
const fs = require('fs');
const path = require('path');
const version = require('../package.json').version;

if (version.includes('-')) {
  console.log(`Storing current version for dev version ${version}`);

  fs.writeFileSync(
    path.join(__dirname, '../meta.json'),
    JSON.stringify({ version: version.split('-')[0] }, null, '\t')
  );
}
