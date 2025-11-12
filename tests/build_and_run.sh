#!/bin/bash
# Build and run MIA unit tests on host machine

set -e  # Exit on error

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Building MIA unit tests...${NC}"

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
make

echo -e "${GREEN}Build successful!${NC}"
echo ""
echo -e "${YELLOW}Running tests...${NC}"
echo ""

# Run tests
./mia_tests

# Capture exit code
TEST_RESULT=$?

echo ""
if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ All tests passed!${NC}"
else
    echo -e "${RED}✗ Some tests failed!${NC}"
fi

exit $TEST_RESULT
