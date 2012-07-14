#!/usr/bin/perl

use strict;
use warnings;
use Getopt::Std;

my %opts;
getopt('I', \%opts);
my $file = $opts{'I'}."/libavcodec/avcodec.h";

(-f "$file") or die "couldn't find libavcodec/avcodec.h!";
open(FILE, '<', $file) or die "couldn't open $file";

print <<EOF;
/* Auto-generated file from libavcodec/avcodec.h */
const struct {
    enum CodecID id;
    const char *flow_def;
} upipe_av_codecs[] = {
EOF

while (<FILE>) {
	if (/^\s*CODEC_ID_([A-Za-z0-9_]*).*$/) {
		next if ($1 eq "NONE" || $1 eq "FIRST_AUDIO" || $1 eq "FIRST_SUBTITLE" || $1 eq "FIRST_UNKNOWN");
		print "    { CODEC_ID_$1, \"".lc($1)."\" },\n";
	}
}

print <<EOF;
    { 0, NULL }
};
EOF

close FILE;
