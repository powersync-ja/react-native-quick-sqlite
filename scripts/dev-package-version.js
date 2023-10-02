/**
 * Dev package version like 0.0.0-dev-hash seem to cause issues with the podspec
 * This script will remove the -dev-hash from the version.
 */
const fs = require('fs');
const path = require('path');
const version = process.argv[2];

if (version.includes('0.0.0')) {
  console.log(`Storing current version for dev version ${version}`);
  const currentPackage = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../package.json'), 'utf-8'));
  // Backup the current version
  fs.writeFileSync(
    path.join(__dirname, '../meta.json'),
    JSON.stringify({ version: currentPackage.version }, null, '\t')
  );
}
