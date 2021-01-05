/*
 * this order MUST be preserved, since enum case_e LOWER and UPPER are used to
 * index the array of cached encodings for the blob object.
 */
VMODENUM(LOWER)
VMODENUM(UPPER)
VMODENUM(DEFAULT)
#undef VMODENUM
