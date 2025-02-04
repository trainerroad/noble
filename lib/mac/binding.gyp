{
  'targets': [
    {
      'target_name': 'binding',
      'sources': [ 'src/noble_mac.mm', 'src/napi_objc.mm', 'src/ble_manager.mm', 'src/objc_cpp.mm', 'src/callbacks.cc'  ],
      'include_dirs': ["<!@(node -p \"require('node-addon-api').include\")"],
      'dependencies': ["<!(node -p \"require('node-addon-api').gyp\")"],
      'cflags!': [ '-fno-exceptions' ],
      'cflags_cc!': [ '-fno-exceptions' ],
      'cflags+': ['-fvisibility=hidden'],
      'xcode_settings': {
        'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES', # -fvisibility=hidden
        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',
        'CLANG_CXX_LIBRARY': 'libc++',
        'MACOSX_DEPLOYMENT_TARGET': '10.7',
        'OTHER_CFLAGS': [
            '-fobjc-arc',
        ],
      },
      'link_settings': {
        'libraries': [
          '$(SDKROOT)/System/Library/Frameworks/CoreBluetooth.framework',
        ]
      },
      'product_dir': '../lib/mac/native',
    }
  ]
}
