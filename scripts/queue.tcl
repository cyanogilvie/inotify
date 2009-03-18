# vim: ft=tcl foldmethod=marker foldmarker=<<<,>>> ts=4 shiftwidth=4

oo::class create inotify::queue {
	variable {*}{
		cb
		queue_handle
		wd_map
		path_map
	}

	constructor {a_cb} { #<<<
		my variable consumer

		set cb			$a_cb
		set wd_map		[dict create]
		set path_map	[dict create]

		set queue_handle	[inotify::create_queue]
		chan configure $queue_handle \
				-blocking 0 \
				-buffering none \
				-translation binary \
				-encoding binary
		set consumer	"::consumer_[string map {:: _} [self]]"
		coroutine $consumer my _readable
		chan event $queue_handle readable [list $consumer]
	}

	#>>>
	destructor { #<<<
		my variable consumer

		if {[info exists queue_handle]} {
			dict for {wd path} $wd_map {
				try {
					inotify::rm_watch $queue_handle $wd
				} on error {errmsg options} {
					log error "Error removing watch on \"$path\": $errmsg"
				}
				dict unset wd_map $wd
			}
			try {
				if {
					[info exists queue_handle] &&
					$queue_handle in [chan names]
				} {
					chan close $queue_handle
				}
			} on error {errmsg options} {
				log error "Error closing inotify queue handle: $errmsg"
			}
		}
		if {[info exists consumer]} {
			rename $consumer {}
		}
	}

	#>>>

	method add_watch {path {mask {IN_ALL_EVENTS}}} { #<<<
		set wd	[inotify::add_watch $queue_handle $path $mask]
		dict set path_map $path	$wd
		dict set wd_map $wd		$path
	}

	#>>>
	method rm_watch {path} { #<<<
		if {![dict exists $path_map $path]} {
			throw [list no_watches $path] "No watches on path \"$path\""
		}
		inotify::rm_watch $queue_handle [dict get $path_map $path]
		dict unset path_map $path
		dict unset wd_map $wd
	}

	#>>>

	method _readable {} { #<<<
		while {1} {
			yield

			if {[chan eof $queue_handle]} {
				log error "Queue handle closed"
				chan close $queue_handle
				unset queue_handle
				my destroy
				return
			}

			set dat		[chan read $queue_handle]
			set events	[inotify::decode_events $dat]

			foreach event $events {
				set wd	[lindex $event 0]
				if {$wd == -1} {
					# This happens when we get a synthetic IN_Q_OVERFLOW event
					set event	[lreplace $event 0 0 {}]
				} elseif {![dict exists $wd_map $wd]} {
					log error "No path map for watch descriptor ($wd)"
					continue
				} else {
					set event	[lreplace $event 0 0 [dict get $wd_map $wd]]
				}

				set arr	{}
				foreach field {path mask cookie name} value $event {
					lappend arr $field $value
				}

				if {[info exists cb]} {
					try {
						uplevel #0 $cb [list $arr]
					} on error {errmsg options} {
						log error "Error invoking callback for event ($arr): $errmsg\n[dict get $options -errorinfo]"
					}
				}
			}
		}
	}

	#>>>
	method log {lvl msg} { #<<<
		puts stderr $msg
	}

	unexport log
	#>>>
}


