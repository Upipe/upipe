#!/usr/bin/env perl

# gen-args.pl
#
# Copyright (C) 2018 Clément Vasseur <clement.vasseur@gmail.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.

use strict;
use warnings;

sub type {
    my $arg = shift;
    $arg =~ s/\*\w+$/*/;
    if ($arg =~ m/(\w+)\s+(\w+)$/
            and $1 !~ /^(enum|struct|union)$/
            and $2 !~ /^(char|short|int|long)$/) {
        $arg =~ s/\s+\w+$//;
    }
    return "\"$arg\"";
}

my $mode = shift;
my ($prefix, $suffix) = $mode =~ m/^(\w+)-(\w+)$/;

if ($mode eq 'upipe-command') {
    print "local ffi = require \"ffi\"\n";
    print "local C = ffi.C\n";
}

print "return {\n";

foreach (@ARGV) {
    next if $mode eq 'upipe-command' and $_ !~ /\bupipe\.h$/;

    open my $fh, '<', $_ or die "$_: $!\n";
    my $src = do { local $/ = <$fh> };
    close $fh;

    while ($src =~ m/\benum\s+${prefix}_(?:\w+_)?$suffix\s*{(.+?)}\s*;/gs) {
        my $body = $1;
        while ($body =~ m/\/\*\* .+? \(([^(]+?)\).*? \*\/\s*\U$prefix\E_([A-Z0-9_]+)/gs) {
            if ($1 ne 'void' and $2 !~ /(^|_)LOCAL$/) {
                my $args = join(', ', map {&type($_)} split(/\s*,\s*/, $1));
                if ($mode eq 'upipe-command') {
                    print "    [C.UPIPE_$2] = { $args },\n";
                } else {
                    print "    $2 = { $args },\n";
                }
            }
        }
    }
}

print "}\n";
