#ifndef __FREQMETER_H__
#define __FREQMETER_H__

#include <Windows.h>
#include <intrin.h>

#define FREQ_MEASUREMENT_ERROR_NO_MEMORY (-1ull << 32) | 0xBAD00001
#define FREQ_MEASUREMENT_ERROR_

/**
 * @brief Convert a TSC value to nanoseconds
 * 
 * @param [in] Value: The TSC value to convert 
*/
UINT64
TSCToNs( 
	_In_ UINT64 Value 
	);

/**
 * @brief Obtain an estimated frequency of a specified logical processor.
 * 
 * @param [in] ProcessorNumber: 
 * 
 * @return The frequency of the measured processor in Hz
 * @returns
*/
UINT64
MeasureThreadFrequency( 
	IN UINT32 ProcessorNumber = -1
	);

/**
 * @brief Attempt to obtain the TSC frequency
 *
 * @return If the TSC frequency was successfully obtained,
 *         the return value is nonzero and is in Hz
 * @return If the TSC frequency is not invariant,
 *         the return value is TSC_FREQUENCY_CYCLES
*/
UINT64 
GetTSCFrequency( 
	VOID 
	);

#endif