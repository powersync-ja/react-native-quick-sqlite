require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))
if (File.exist?(File.join(__dir__, "meta.json")))
  meta = JSON.parse(File.read(File.join(__dir__, "meta.json")))
  package["version"] = meta["version"]
end

Pod::Spec.new do |s|
  s.name         = "react-native-quick-sqlite"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => "10.0" }
  s.source       = { :git => "https://github.com/margelo/react-native-quick-sqlite.git", :tag => "#{s.version}" }

  s.pod_target_xcconfig = {
    :GCC_PREPROCESSOR_DEFINITIONS => "HAVE_FULLFSYNC=1 SQLITE_ENABLE_FTS5=1",
    :WARNING_CFLAGS => "-Wno-shorten-64-to-32 -Wno-comma -Wno-unreachable-code -Wno-conditional-uninitialized -Wno-deprecated-declarations",
    :USE_HEADERMAP => "No"
  }

  s.header_mappings_dir = "cpp"
  s.source_files = "ios/**/*.{h,hpp,m,mm}", "cpp/**/*.{h,cpp,c}"

  s.dependency "React-callinvoker"
  s.dependency "React"
  s.dependency "powersync-sqlite-core", "0.4.4"
  if defined?(install_modules_dependencies())
    install_modules_dependencies(s)
  else
    s.dependency "React-Core"
  end

  if ENV['QUICK_SQLITE_USE_PHONE_VERSION'] == '1' then
    s.exclude_files = "cpp/sqlite3.c", "cpp/sqlite3.h"
    s.library = "sqlite3"
  end

end
