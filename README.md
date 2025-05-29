# FreqMeter
FreqMeter is a lightweight per-core CPU frequency meter which can be utilized for performance measurements. Seeing as each executing logical processor can operate at an independent frequency and the TSC counter is usually invariant, this provides a solution for timing measurements in clock cycle approximations, as opposed to TSC cycles.

### APIs
```cpp
/**
 * @brief Convert a TSC value to nanoseconds
 * 
 * @param [in] Value: The TSC value to convert 
*/
UINT64
TSCToNs( 
	_In_ UINT64 Value 
	);
```
