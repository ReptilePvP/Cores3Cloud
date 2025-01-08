# Changelog

All notable changes to the Terp Meter project will be documented in this file.

## [2025-01-07]

### Added
- Enhanced debug logging system
  - Added structured temperature debug output
  - Added memory monitoring and stack checking
  - Added detailed debug information functions
- Created development branch file (Cores3_Arduino_Cloud_dev.ino) for testing changes

### Changed
- Improved temperature reading display
  - Added formatted debug output with [Debug Temperature] section
  - Shows Raw, Celsius, Fahrenheit, and Display temperatures
  - Display temperature now shows as whole numbers
- Simplified temperature checking logic
  - Moved debug output to readTemperature function
  - Streamlined checkTemperature function

### Fixed
- Added brightness safeguard to prevent screen from going completely dark
  - Set minimum brightness threshold to 25
  - Improved settings loading sequence in setup

## [2025-01-06]

### Added
- Temperature unit selection screen on first boot
  - Added `selectTemperatureUnit()` function for C/F selection
  - Added persistent storage of temperature unit preference
  - Unit selection appears after sensor initialization
- Remote monitoring capabilities
  - Integrated Arduino Cloud connectivity
  - Added real-time temperature monitoring through Arduino IoT Cloud
  - Remote access to device readings via web dashboard or mobile app
  - Secure data transmission with Arduino Cloud encryption
- Cloud Connectivity Toggle in Settings
  - Added option to enable/disable cloud connectivity
  - Visual feedback for cloud connection status
  - Persistent cloud preference storage
  - Automatic cloud reconnection handling

### Changed
- Modified settings menu structure
  - Removed temperature unit option from settings menu
  - Removed target temperature option from settings menu
  - Removed tolerance option from settings menu
  - Added cloud connectivity toggle
  - Settings menu now shows Sound, Brightness, Emissivity, and Cloud options

### Fixed
- Improved touch response in temperature unit selection screen
  - Added proper button state handling
  - Added visual feedback when buttons are pressed
  - Fixed button enabled states

### Technical Details
- Added new preference key "unitSelected" to track first boot
- Modified Button class usage in selectTemperatureUnit
- Updated SettingsMenu structure
  - Reduced numItems from 6 to 4
  - Added cloud toggle option
  - Added cloud state management
  - Implemented cloud connection handling
- Maintained existing temperature unit, target, and tolerance settings in Settings structure for future use
- Implemented Arduino Cloud connectivity
  - Added WiFi credentials management
  - Configured cloud variables for temperature data
  - Set up secure connection to Arduino IoT Cloud
  - Added data synchronization handling
  - Added dynamic cloud connection management

## [Previous Changes]

### Added
- Emissivity adjustment interface
  - Added detailed emissivity control with min (0.65) and max (1.00) limits
  - Added confirmation screen for emissivity changes
  - Added restart requirement after emissivity changes
  - Added visual feedback for value changes

### Temperature Control Features
- Implemented temperature range monitoring
  - Celsius ranges: 304°C to 371°C (target: 338°C, tolerance: ±5°C)
  - Fahrenheit ranges: 580°F to 700°F (target: 640°F, tolerance: ±9°F)
  - Added temperature status display with color coding

### UI Components
- Added battery status indicator
  - Positioned in header at width - 45px
  - Visual battery level representation
- Added toggle switch component
  - Customizable width (50px) and height (24px)
  - Animated state changes
  - Visual feedback for on/off states

### Technical Improvements
- Implemented touch debouncing for better button response
- Added audio feedback for user interactions
- Optimized display updates to prevent screen flicker
  - Only updating changed values
  - Implemented partial screen refreshes
- Added error handling and validation for sensor readings
