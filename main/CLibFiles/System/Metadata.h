#ifndef SYSTEM_METADATA_H
#define SYSTEM_METADATA_H

#include "System/System.h"

// Descriptor for a contiguous segment of memory image.
typedef struct
{
  // The address of the segment.
  uint32 Address;
  
  // The length of the segment, in bytes. The checksum for the segment, if present, 
  // will be located at Address + Length.
  uint32 Length;
}
MetadataSegmentDescriptor;

/// Metadata parameter
typedef struct
{
  char Data[32];
}
MetadataParameter;

typedef struct
{
  /// Value that uniquely identifies the metadata.
  uint32 Identifier;
  
  /// Length of the metadata structure, in bytes.
  uint32 Length;
  
  // The number of segments in the image.
  uint32 SegmentCount;
  
  /// The number of parameters contained in the metadata.
  uint32 ParameterCount;
}
MetadataHeader;

#endif // SYSTEM_METADATA_H
