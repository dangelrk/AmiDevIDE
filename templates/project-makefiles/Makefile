# Makefile - Projektspezifische Einstellungen
# Diese Datei enthält nur projekt-spezifische Konfiguration

#############################################
# Project Configuration - HIER ANPASSEN!
#############################################

PROJECT = testproject
VERSION = 1.0

# Alle C-Dateien (in beliebiger Reihenfolge)
C_SOURCES = main.c utils.c

# Alle Assembly-Dateien (.s oder .asm)
ASM_SOURCES = startup.s

# CPU Target
CPU = 68000

# Optimierungs-Level (0, 1, 2, 3)
OPT_LEVEL = 2

# Debug-Build? (0 = Release, 1 = Debug)
DEBUG = 0

#############################################
# Project Paths - DEINE ORDNER
#############################################

# Deine Include-Ordner
PROJECT_INCLUDES = -I./include

# Deine Library-Ordner
PROJECT_LIBDIRS = 

# Deine Libraries (ohne -l Prefix angeben)
# Beispiel: EXTRA_LIBS = mui amissl
EXTRA_LIBS = 

# Output directories
BINDIR = bin
OBJDIR = obj

#############################################
# Platform-spezifische Einstellungen laden
#############################################

include Makefile.platform

#############################################
# Compiler & Linker Flags
#############################################

# C Compiler Flags
CFLAGS = +aos68k \
         -c99 \
         -cpu=$(CPU) \
         -I$(VBCC_INC) \
         -I$(NDK32_INC) \
         -I$(MUI38_INC) \
         -I$(TCP_INC) \
         -I$(NET_INC) \
         -I$(AMISSL_INC) \
         -I$(SQLITE_INC) \
         -I$(POSIXLIB_INC) \
         $(PROJECT_INCLUDES)

# Optimierung hinzufügen
ifeq ($(DEBUG),1)
    CFLAGS += -g
else
    CFLAGS += -O$(OPT_LEVEL)
endif

# Assembler Flags
ASFLAGS = -Fhunk \
          -m$(CPU) \
          -kick1hunks \
          -I$(NDK32_ASM_INC) \
          $(PROJECT_INCLUDES)

# Debug-Symbole für Assembly
ifeq ($(DEBUG),1)
    # Mit Symbolen
else
    ASFLAGS += -nosym
endif

# Linker Flags
LDFLAGS = +aos68k \
          -cpu=$(CPU) \
          -L$(VBCC_LIB) \
          -L$(NDK32_LIB) \
          -L$(AMISSL_LIB) \
          -L$(SQLITE_LIB) \
          $(PROJECT_LIBDIRS)

# Libraries zusammenbauen
LIBS = -lamiga $(addprefix -l, $(EXTRA_LIBS))

#############################################
# Build System - NICHT ÄNDERN
#############################################

include Makefile.rules