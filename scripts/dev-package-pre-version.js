/**
 * Dev package versions like 0.0.0-dev-[timestamp] seem to cause issues with the podspec
 * This script will remove the -dev-[timestamp] from the version.
 * Side Note: Versions above 0.0.0 e.g. 0.0.1-dev-[timestamp] do work with pod install.
 */
const fs = require('fs');
const path = require('path');

console.log(`Storing current version for dev version`);
const currentPackage = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../package.json'), 'utf-8'));
// Backup the current version
fs.writeFileSync(path.join(__dirname, '../meta.json'), JSON.stringify({ version: currentPackage.version }, null, '\t'));
