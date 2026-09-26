; First code in the segment. Running a library from the Shell must fail
; cleanly, so entry point zero just returns -1; exec never calls it, it
; finds the romtag instead.
	xdef	_start
	SECTION	libstart,CODE
_start:
	moveq	#-1,d0
	rts
	END
