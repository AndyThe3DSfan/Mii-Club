# Mii Club 3DS - devkitPro/devkitARM project
# Requires the 3DS development packages from devkitPro.

TARGET   := miiclub
BUILD    := build
SOURCES  := source
INCLUDES := -I$(DEVKITPRO)/libctru/include

#---------------------------------------------------------------------------------
# Basic devkitPro 3DS build setup
#---------------------------------------------------------------------------------
include $(DEVKITPRO)/devkitARM/base_rules

CXXFLAGS := -MMD -MP -MF $(@:.o=.d) -O2 -Wall -Wextra -std=gnu++17 -fno-rtti -fno-exceptions
CFLAGS   := -MMD -MP -MF $(@:.o=.d) -O2 -Wall -Wextra

# FIXED: Added the required 3dsx specs flag so the 3DS system handles link properly!
LIBS     := -specs=3dsx.specs -L$(DEVKITPRO)/libctru/lib -lctru

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(notdir $(wildcard $(SOURCES)/*.cpp)))

.PHONY: all clean

all: $(TARGET).3dsx

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: $(SOURCES)/%.cpp | $(BUILD)
	$(CXX) $(ARCH) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(CXX) $(ARCH) $(OBJECTS) $(LIBS) -o $@

$(TARGET).3dsx: $(TARGET).elf
	3dsxtool $< $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).3dsx

-include $(BUILD)/*.d
