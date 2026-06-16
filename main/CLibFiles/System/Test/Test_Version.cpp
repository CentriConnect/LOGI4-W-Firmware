#include "Test_Version.h"

#include <string.h>
#include "Test.h"
#include "Version.h"

void Test_Version()
{
	Version v = { 1, 2, 3 };
	TEST(v.Major == 1);
	TEST(v.Minor == 2);
	TEST(v.Revision == 3);

	Version_Clear(&v);
	TEST(v.Major == 0);
	TEST(v.Minor == 0);
	TEST(v.Revision == 0);

	Version_Initialize(&v, 4, 5, 6);
	TEST(v.Major == 4);
	TEST(v.Minor == 5);
	TEST(v.Revision == 6);

	Version_Initialize(&v, 255, 254, 253);
	TEST(v.Major == 255);
	TEST(v.Minor == 254);
	TEST(v.Revision == 253);

	byte array[VERSION_BINARY_SIZE] = { 1, 2, 3 };
	TEST(Version_FromByteArray(&v, array, sizeof(array)));
	TEST(v.Major == 1);
	TEST(v.Minor == 2);
	TEST(v.Revision == 3);
	TEST(array[0] == 1);
	TEST(array[1] == 2);
	TEST(array[2] == 3);

	Version_Initialize(&v, 8, 9, 10);
	TEST(Version_ToByteArray(&v, array, sizeof(array)));
	TEST(v.Major == 8);
	TEST(v.Minor == 9);
	TEST(v.Revision == 10);
	TEST(array[0] == 8);
	TEST(array[1] == 9);
	TEST(array[2] == 10);

	byte short_array[VERSION_BINARY_SIZE - 1];
	TEST(Version_ToByteArray(&v, short_array, sizeof(short_array)) == false);
	TEST(v.Major == 8);
	TEST(v.Minor == 9);
	TEST(v.Revision == 10);
	TEST(array[0] == 8);
	TEST(array[1] == 9);
	TEST(array[2] == 10);

	TEST(Version_FromByteArray(&v, short_array, sizeof(short_array)) == false);
	TEST(v.Major == 8);
	TEST(v.Minor == 9);
	TEST(v.Revision == 10);
	TEST(array[0] == 8);
	TEST(array[1] == 9);
	TEST(array[2] == 10);

	byte long_array[VERSION_BINARY_SIZE + 1] = { 1, 2, 3, 4 };
	TEST(Version_FromByteArray(&v, long_array, sizeof(long_array)));
	TEST(v.Major == 1);
	TEST(v.Minor == 2);
	TEST(v.Revision == 3);
	TEST(long_array[0] == 1);
	TEST(long_array[1] == 2);
	TEST(long_array[2] == 3);
	TEST(long_array[3] == 4);

	Version_Initialize(&v, 8, 9, 10);
	TEST(Version_ToByteArray(&v, long_array, sizeof(long_array)));
	TEST(v.Major == 8);
	TEST(v.Minor == 9);
	TEST(v.Revision == 10);
	TEST(long_array[0] == 8);
	TEST(long_array[1] == 9);
	TEST(long_array[2] == 10);
	TEST(long_array[3] == 4);

	TEST(Version_FromByteArray(&v, nullptr, sizeof(array)) == false);
	TEST(Version_ToByteArray(&v, nullptr, sizeof(array)) == false);

	TEST(Version_FromByteArray(nullptr, array, sizeof(array)) == false);
	TEST(Version_ToByteArray(nullptr, array, sizeof(array)) == false);

	char string[VERSION_MAX_STRING_SIZE];
	Version_Initialize(&v, 1, 2, 3);
	TEST(Version_ToString(&v, string, sizeof(string)));
	TEST(strcmp(string, "1.2.3") == 0);

	Version_Initialize(&v, 2, 3, 0);
	TEST(Version_ToString(&v, string, sizeof(string)));
	TEST(strcmp(string, "2.3") == 0);

	Version_Initialize(&v, 255, 254, 253);
	TEST(Version_ToString(&v, string, sizeof(string)));
	TEST(strcmp(string, "255.254.253") == 0);

	char short_string[VERSION_MAX_STRING_SIZE - 1];
	TEST(!Version_ToString(&v, short_string, sizeof(short_string)));

	char long_string[VERSION_MAX_STRING_SIZE + 1];
	long_string[VERSION_MAX_STRING_SIZE] = 42;
	Version_Initialize(&v, 1, 2, 3);
	TEST(Version_ToString(&v, long_string, sizeof(long_string)));
	TEST(strcmp(long_string, "1.2.3") == 0);
	TEST(long_string[VERSION_MAX_STRING_SIZE] == 42);

	Version_Initialize(&v, 2, 3, 0);
	TEST(Version_ToString(&v, long_string, sizeof(long_string)));
	TEST(strcmp(long_string, "2.3") == 0);
	TEST(long_string[VERSION_MAX_STRING_SIZE] == 42);

	Version_Initialize(&v, 255, 254, 253);
	TEST(Version_ToString(&v, long_string, sizeof(long_string)));
	TEST(strcmp(long_string, "255.254.253") == 0);
	TEST(long_string[VERSION_MAX_STRING_SIZE] == 42);

	Version_Initialize(nullptr, 1, 2, 3);
	TEST(!Version_FromByteArray(nullptr, array, sizeof(array)));
	TEST(!Version_FromByteArray(&v, nullptr, sizeof(array)));
	TEST(!Version_ToByteArray(nullptr, array, sizeof(array)));
	TEST(!Version_ToByteArray(&v, nullptr, sizeof(array)));
}
