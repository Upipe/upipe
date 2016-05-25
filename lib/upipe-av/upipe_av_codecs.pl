#!/usr/bin/perl

use strict;
use warnings;
use Getopt::Std;

my $file = "$ARGV[0]";

(-f "$file") or die "couldn't find $file !";
open(FILE, '-|', "$ENV{'CPP'} $ENV{'CFLAGS'} \"$file\"") or die "couldn't open $file";

print <<EOF;
/* Auto-generated file from libavcodec/avcodec.h */
static const struct {
    enum AVCodecID id;
    const char *flow_def;
} upipe_av_codecs[] = {
EOF

my $suffix = ".pic.";

while (<FILE>) {
	if (/^\s*((AV_)?CODEC_ID_)([A-Za-z0-9_]*).*$/) {
		my $enumprefix = $1;
		my $codec = $3;
		if ($codec eq "FIRST_AUDIO") {
			$suffix = ".sound.";
		} elsif ($codec eq "FIRST_SUBTITLE") {
			$suffix = ".pic.sub.";
		}
		next if ($codec eq "NONE" || $codec eq "FIRST_AUDIO" || $codec eq "FIRST_SUBTITLE" || $codec eq "FIRST_UNKNOWN");
		print "    { $enumprefix$codec, \"".lc($codec).$suffix."\" },\n";
	}
}

print <<EOF;
    { 0, NULL }
};
EOF

close FILE;
