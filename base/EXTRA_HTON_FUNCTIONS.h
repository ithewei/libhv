/*
 * Byte order conversion functions for 64-bit integers and 32 + 64 bit
 * floating-point numbers.  IEEE big-endian format is used for the
 * network floating point format.
 */
#define _WS2_32_WINSOCK_SWAP_LONG(l)                \
            ( ( ((l) >> 24) & 0x000000FFL ) |       \
              ( ((l) >>  8) & 0x0000FF00L ) |       \
              ( ((l) <<  8) & 0x00FF0000L ) |       \
              ( ((l) << 24) & 0xFF000000L ) )

#define _WS2_32_WINSOCK_SWAP_LONGLONG(l)            \
            ( ( ((l) >> 56) & 0x00000000000000FFLL ) |       \
              ( ((l) >> 40) & 0x000000000000FF00LL ) |       \
              ( ((l) >> 24) & 0x0000000000FF0000LL ) |       \
              ( ((l) >>  8) & 0x00000000FF000000LL ) |       \
              ( ((l) <<  8) & 0x000000FF00000000LL ) |       \
              ( ((l) << 24) & 0x0000FF0000000000LL ) |       \
              ( ((l) << 40) & 0x00FF000000000000LL ) |       \
              ( ((l) << 56) & 0xFF00000000000000LL ) )


#ifndef htonll
__inline unsigned __int64 htonll ( unsigned __int64 Value )
{
	const unsigned __int64 Retval = _WS2_32_WINSOCK_SWAP_LONGLONG (Value);
	return Retval;
}
#endif /* htonll */

#ifndef ntohll
__inline unsigned __int64 ntohll ( unsigned __int64 Value )
{
	const unsigned __int64 Retval = _WS2_32_WINSOCK_SWAP_LONGLONG (Value);
	return Retval;
}
#endif /* ntohll */

#ifndef htonf
__inline unsigned __int32 htonf ( float Value )
{
	unsigned __int32 Tempval;
	unsigned __int32 Retval;
	Tempval = *(unsigned __int32*)(&Value);
	Retval = _WS2_32_WINSOCK_SWAP_LONG (Tempval);
	return Retval;
}
#endif /* htonf */

#ifndef ntohf
__inline float ntohf ( unsigned __int32 Value )
{
	const unsigned __int32 Tempval = _WS2_32_WINSOCK_SWAP_LONG (Value);
	float Retval;
	*((unsigned __int32*)&Retval) = Tempval;
	return Retval;
}
#endif /* ntohf */

#ifndef htond
__inline unsigned __int64 htond ( double Value )
{
	unsigned __int64 Tempval;
	unsigned __int64 Retval;
	Tempval = *(unsigned __int64*)(&Value);
	Retval = _WS2_32_WINSOCK_SWAP_LONGLONG (Tempval);
	return Retval;
}
#endif /* htond */

#ifndef ntohd
__inline double ntohd ( unsigned __int64 Value )
{
	const unsigned __int64 Tempval = _WS2_32_WINSOCK_SWAP_LONGLONG (Value);
	double Retval;
	*((unsigned __int64*)&Retval) = Tempval;
	return Retval;
}
#endif /* ntohd */
