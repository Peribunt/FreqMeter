#include "FreqMeter.h"

//
// Disable unary minus operator warning on unsigned values.
//
#pragma warning( disable : 4146 )

typedef union _CPUID_REGS
{
	INT32 Data[ 4 ];

	struct
	{
		UINT32 EAX;
		UINT32 EBX;
		UINT32 ECX;
		UINT32 EDX;
	};
}CPUID_REGS, *PCPUID_REGS;

typedef UINT32
( *GetDeltaSample_t )(
	VOID
	);

UINT8 GetDeltaSample_Prologue[ ] =
{
	/*0x00:*/ 0x53,             //push rbx
	/*0x01:*/ 0x0F, 0x01, 0xF9, //rdtscp
	/*0x04:*/ 0x48, 0x89, 0xC3, //mov  rbx, rax
	/*0x07:*/ 0x0F, 0xAE, 0xE8, //lfence
	/*...  */
};

UINT8 GetDeltaSample_Epilogue[ ] =
{
	/*...+0x00:*/ 0x0F, 0x01, 0xF9,                         //rdtscp
	/*...+0x03:*/ 0x0F, 0xAE, 0xE8,                         //lfence
	/*...+0x06:*/ 0x48, 0x81, 0xEB, 0xE8, 0x03, 0x00, 0x00, //sub rbx, 1000
	/*...+0x0D:*/ 0x48, 0x29, 0xD8,                         //sub rax, rbx
	/*...+0x10:*/ 0x5B,                                     //pop rbx
	/*...+0x11:*/ 0xC3                                      //ret
};

/**
 * @brief Use CPUID to attempt to obtain the TSC frequency
 * 
 * @return If the TSC frequency was successfully obtained,
 *         the return value is nonzero and is in Hz
 * @return If the TSC frequency is not invariant,
 *         the return value is TSC_FREQUENCY_CYCLES
 * @return If the TSC frequency was not successfully obtained,
 *         the return value is TSC_FREQUENCY_UNKNOWN
*/
DECLSPEC_NOINLINE
UINT64
CPUID_GetTSCFrequency( 
	VOID 
	)
#define TSC_FREQUENCY_CYCLES  -1ull
#define TSC_FREQUENCY_UNKNOWN 0
{
	CPUID_REGS Regs;

	//
	// CPU Power management and RAS capabilities
	//
	__cpuid( Regs.Data, 0x80000007 );

	//
	// [Bit 8:7] Tsc invariant. If this bit is not set, the TSC is not invariant
	//           across P-states, C-states and is to be considered the core frequency
	//
	if ( ( ( Regs.EDX >> 8 ) & 1 ) == NULL ) {
		return -1ull;
	}

	//
	// TSC and core crystal clock data
	//
	__cpuid( Regs.Data, 0x15 );

	if ( Regs.EBX && Regs.ECX ) {
		//
		// If EBX and ECX are nonzero, the TSC frequency is given by doing the following:
		// 
		// ECX * (EBX / EAX)
		//
		return Regs.ECX * ( Regs.EBX / Regs.EAX );
	}

	if ( Regs.ECX == NULL )
	{
		//
		// CPU and bus specification frequency data.
		//
		__cpuid( Regs.Data, 0x16 );

		if ( Regs.EAX ) {
			//
			// If CPUID_0x15.ECX is zero and CPUID_0x15.EAX is nonzero,
			// the TSC frequency is the same as the processor base frequency.
			//
			return Regs.EAX * 10000000;
		}
	}

	return TSC_FREQUENCY_UNKNOWN;
}

UINT64 g_TSCFrequency = NULL;
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
	)
{
	if ( g_TSCFrequency != NULL ) {
		return g_TSCFrequency;
	}

	UINT64 Frequency     = CPUID_GetTSCFrequency( ),
		   PerfFreq      = 0,
		   PerfCount1    = 0,
		   PerfCount2    = 0;
	HANDLE CurrentThread = GetCurrentThread( );
	UINT32 Priority      = GetThreadPriority( CurrentThread ),
		   TSC_AUX       = 0;

	if ( Frequency != TSC_FREQUENCY_UNKNOWN ) {
		return Frequency;
	}

	if ( Frequency == TSC_FREQUENCY_CYCLES ) {
		return TSC_FREQUENCY_CYCLES;
	}

	//
	// Set our thread priority to time critical for a more accurate measurement
	//
	SetThreadPriority( CurrentThread, THREAD_PRIORITY_TIME_CRITICAL );

	//
	// Prepare performance counter data for a delta measurement
	//
	QueryPerformanceFrequency( ( PLARGE_INTEGER )&PerfFreq   );
	QueryPerformanceCounter  ( ( PLARGE_INTEGER )&PerfCount1 );
	
	PerfCount2 = PerfCount1;

	//
	// Current TSC count
	//
	Frequency = __rdtscp( &TSC_AUX );

	while ( ( PerfCount2 - PerfCount1 ) < PerfFreq ) {
		QueryPerformanceCounter( ( PLARGE_INTEGER )&PerfCount2 );
	}

	//
	// Approximation of the TSC frequency
	//
	Frequency = __rdtscp( &TSC_AUX ) - Frequency;

	//
	// Reset our thread priority
	//
	SetThreadPriority( CurrentThread, Priority );

	g_TSCFrequency = Frequency - ( Frequency % 10000000 );

	return g_TSCFrequency;
}

GetDeltaSample_t g_GetDeltaSample = NULL;
UINT64
MeasureThreadFrequency( 
	IN UINT32 ProcessorNumber
	)
{
	HANDLE CurrentThread = GetCurrentThread( );
	UINT32 Fast          = MAXUINT,
		   Priority      = GetThreadPriority( CurrentThread ),
		   Affinity      = NULL;

	if ( g_TSCFrequency == NULL ) {
		 g_TSCFrequency = GetTSCFrequency( );
	}

	if ( g_GetDeltaSample == NULL ) 
	{ 
		UINT32 Position = NULL;

		//
		// Allocate some space for our clock cycle measurment function
		//
		UINT8* GetDeltaSample_Data = ( UINT8* )VirtualAlloc( 
			NULL, 
			0x2000, 
			MEM_COMMIT | MEM_RESERVE, 
			PAGE_EXECUTE_READWRITE 
			);

		if ( GetDeltaSample_Data == NULL ) {
			return FREQ_MEASUREMENT_ERROR_NO_MEMORY;
		}

		//
		// Copy the start of the data for the clock cycle measurement function
		//
		RtlCopyMemory( 
			GetDeltaSample_Data, 
			GetDeltaSample_Prologue, 
			sizeof( GetDeltaSample_Prologue ) 
			);
		Position += sizeof( GetDeltaSample_Prologue );

		//
		// Pad with ~1000 clock cycles worth of increments
		//
		__stosd(
			( PDWORD )( GetDeltaSample_Data + Position ),
			0xC3FFC3FF,
			500
			);
		Position += 500 * sizeof( UINT32 );

		//
		// Copy the start of the data for the clock cycle measurement function
		//
		RtlCopyMemory( 
			GetDeltaSample_Data + Position, 
			GetDeltaSample_Epilogue, 
			sizeof( GetDeltaSample_Epilogue )
			);

		g_GetDeltaSample = ( GetDeltaSample_t )GetDeltaSample_Data;
	}

	//
	// Obtain a new time slice for our thread
	//
	Sleep( 0 );

	if ( ProcessorNumber == -1ul ) {
		 //
		 // Current executing processor number
		 //
		 __rdtscp( &ProcessorNumber );
	}

	Affinity = SetThreadAffinityMask( CurrentThread, 1ul << ProcessorNumber );
	           SetThreadPriority    ( CurrentThread, THREAD_PRIORITY_TIME_CRITICAL );

	for ( UINT32 i = NULL; i < 100000; i++ ) 
	{
		//
		// Measure the TSC delta of ~1000-1100 clock cycles
		//
		UINT32 Current = g_GetDeltaSample( );

		if ( Current < Fast ) {
			 Fast    = Current;
		}
	}
	
	SetThreadAffinityMask( CurrentThread, Affinity );
	SetThreadPriority    ( CurrentThread, Priority );

	return ( g_TSCFrequency / Fast ) * 1000;
}

UINT64
TSCToNs( 
	_In_ UINT64 Value 
	)
{
	return ( Value * 1000000000 ) / GetTSCFrequency( );
}
