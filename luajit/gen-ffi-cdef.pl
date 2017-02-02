#!/usr/bin/env perl

# gen-ffi-cdef.pl
#
# Copyright (C) 2016 Clément Vasseur <clement.vasseur@gmail.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.

use strict;
use warnings;
use Getopt::Long;

my @builtins = qw(
  va_list __builtin_va_list __gnuc_va_list
  int8_t int16_t int32_t int64_t
  uint8_t uint16_t uint32_t uint64_t
  intptr_t uintptr_t ptrdiff_t size_t wchar_t
);

my %aliases = ( _Bool => 'bool' );

my %defs = map {$_ => 1} @builtins;
my $enum_value;

# workaround for x86_64 va_list
$aliases{'struct __va_list_tag *'} = 'va_list';
$defs{'struct __va_list_tag'} = 1;

sub parse_input_objdump {
  my %info;
  my @path;

  while (<>) {
    if (m/^ <(\d+)><([[:xdigit:]]+)>: Abbrev Number: \d+ \(DW_TAG_(\S+)\)$/) {
      my $id = "$ARGV/$2";
      $info{$id}{id} = $id;
      $info{$id}{type} = $3;
      pop @path while $1 < @path;
      push @{$path[-1]->{children}}, $info{$id} if @path > 0;
      push @path, $info{$id};

    } elsif (m/^    <[[:xdigit:]]+>\s+DW_AT_(\S+)\s*: (.+?)\s*$/) {
      my ($name, $val) = ($1, $2);
      if ($val =~ m/^<0x([[:xdigit:]]+)>$/) {
        my $id = "$ARGV/$1";
        $info{$id}{id} = $id;
        $path[-1]->{attr}{$name} = $info{$id};
      } elsif ($val =~ m/^0x([[:xdigit:]]+)$/) {
        $path[-1]->{attr}{$name} = hex $1;
      } elsif ($val =~ m/^\(indirect string, offset: 0x[[:xdigit:]]+\): (.+)$/ or
               $val =~ m/^(\S+)$/) {
        $path[-1]->{attr}{$name} = $1;
      }
    }
  }

  return \%info;
}

sub parse_input_eu_readelf {
  my %info;
  my @path;

  while (<>) {
    if (m/^ \[\s*([[:xdigit:]]+)\](\s*)(\S+)$/) {
      my $id = "$ARGV/$1";
      $info{$id}{id} = $id;
      $info{$id}{type} = $3;
      pop @path while length $2 <= @path * 2;
      push @{$path[-1]->{children}}, $info{$id} if @path > 0;
      push @path, $info{$id};

    } elsif (m/^ {8}\s*(\S+)\s*(.+)$/) {
      my ($name, $val) = ($1, $2);
      if ($val =~ m/^\(string\) "(.*)"$/ or
          $val =~ m/^\(strp\) "(.*)"$/ or
          $val =~ m/^\(data[124]\) (.*)$/ or
          $val =~ m/^\(sdata\) (.*)$/ or
          $val =~ m/^\(flag_present\) (.*)$/) {
        $path[-1]->{attr}{$name} = $1;
      } elsif ($val =~ m/^\(ref4\) \[\s*([[:xdigit:]]+)\]$/) {
        my $id = "$ARGV/$1";
        $info{$id}{id} = $id;
        $path[-1]->{attr}{$name} = $info{$id};
      }
    }
  }

  return \%info;
}

sub parse_input_llvm_dwarfdump {
  my %info;
  my @path;

  while (<>) {
    if (m/^0x0*([[:xdigit:]]+): (\s*)DW_TAG_(\S+)/) {
      my $id = "$ARGV/$1";
      $info{$id}{id} = $id;
      $info{$id}{type} = $3;
      pop @path while length $2 < @path * 2;
      push @{$path[-1]->{children}}, $info{$id} if @path > 0;
      push @path, $info{$id};

    } elsif (m/^\s*DW_AT_(\S+) \[DW_FORM_(\S+)\]\s*\((.+)\)$/) {
      my ($name, $form, $val) = ($1, $2, $3);
      if ($form eq 'strp' and $val =~ m/"(.*)"$/ or
          $form =~ m/^data[124]$/ and $val =~ m/^(\d+)$/ or
          $form eq 'sdata' and $val =~ m/^(\d+)$/ or
          $form eq 'flag' and $val =~ m/^0x0(1)$/) {
        $path[-1]->{attr}{$name} = $1;
      } elsif ($form eq 'ref4' and $val =~ m/{0x0*([[:xdigit:]]+)}$/) {
        my $id = "$ARGV/$1";
        $info{$id}{id} = $id;
        $path[-1]->{attr}{$name} = $info{$id};
      }
    }
  }

  return \%info;
}

sub alias {
  my ($t) = @_;
  $aliases{$t} || $t;
}

sub pre {
  my ($prefix, $str) = @_;
  defined $str ? $prefix . $str : '';
}

sub once {
  my ($name, $cb) = @_;
  unless ($defs{$name}) {
    $defs{$name} = 2;
    print &$cb(), ";\n";
  }
}

sub p {
  my ($e, $name) = @_;
  return 'void' . &pre(' ', $name) unless defined $e;
  my $t = $e->{type};
  my $a = $e->{attr};

  my $params = sub {
    my @entries =
      map {&alias(&p($_))}
      grep {$_->{type} eq 'formal_parameter' or
            $_->{type} eq 'unspecified_parameters'}
      @{$e->{children}};
    push @entries, 'void' if @entries == 0;
    pop @entries if @entries == 1 and $entries[0] eq '...';
    # workaround gcc 6.1.0 bug
    pop @entries if @entries >= 2 and $entries[-1] eq '...' and $entries[-2] eq '...';
    sprintf "(%s)", join(', ', @entries);
  };

  my $tname = sub {
    &alias($a->{name}) . &pre(' ', $name);
  };

  my $p_prepend = sub {
    my ($prefix, $sep) = @_;
    &p($a->{type}, $prefix . &pre($sep || '', $name));
  };

  my $typedef = sub {
    unless ($defs{$a->{name}}) {
      my $td = &p($a->{type}, $a->{name});
      unless ($defs{$a->{name}}) {
        print 'typedef ', $td, ";\n";
        $defs{$a->{name}} = 2;
      }
    }
    &$tname();
  };

  my $ch = sub {
    my ($ct, $sep, $term) = @_;
    join $sep || '',
      map {&p($_) . ($term || '')}
      grep {$_->{type} eq $ct}
      @{$e->{children}};
  };

  my $p_children = sub {
    my ($kw, $ct, $sep, $term) = @_;
    if (defined $a->{name}) {
      &once("$kw $a->{name}", sub {
        $enum_value = 0;
        "$kw $a->{name}" . (defined $e->{children} ?
          sprintf(" { %s }", &$ch($ct, $sep, $term)) : '');
      });
      $kw . ' ' . &$tname();
    } else {
      sprintf("$kw { %s }", &$ch($ct, $sep, $term)) . &pre(' ', $name);
    }
  };

  my $enumerator = sub {
    my $value = '';
    if ($a->{const_value} != $enum_value++) {
      $enum_value = $a->{const_value} + 1;
      $value = " = $a->{const_value}";
    }
    "$a->{name}$value";
  };

  my $subrange_type = sub {
    if (defined $a->{count}) {
      sprintf '[%s]', $a->{count};
    } else {
      sprintf '[%s]', defined $a->{upper_bound} ? $a->{upper_bound} + 1 : '';
    }
  };

     if ($t eq 'base_type')              {&$tname()}
  elsif ($t eq 'pointer_type')           {&$p_prepend('*')}
  elsif ($t eq 'restrict_type')          {&$p_prepend('restrict', ' ')}
  elsif ($t eq 'volatile_type')          {&$p_prepend('volatile', ' ')}
  elsif ($t eq 'const_type')             {&$p_prepend('const', ' ')}
  elsif ($t eq 'structure_type')         {&$p_children('struct', 'member', ' ', ';')}
  elsif ($t eq 'union_type')             {&$p_children('union', 'member', ' ', ';')}
  elsif ($t eq 'enumeration_type')       {&$p_children('enum', 'enumerator', ', ')}
  elsif ($t eq 'array_type')             {&p($a->{type}, $name . &$ch('subrange_type'))}
  elsif ($t eq 'subrange_type')          {&$subrange_type()}
  elsif ($t eq 'subroutine_type')        {&p($a->{type}, "($name)" . &$params())}
  elsif ($t eq 'typedef')                {&$typedef()}
  elsif ($t eq 'enumerator')             {&$enumerator()}
  elsif ($t eq 'member')                 {&p($a->{type}, $a->{name}) . &pre(':', $a->{bit_size})}
  elsif ($t eq 'subprogram')             {&p($a->{type}, $a->{name} . &$params())}
  elsif ($t eq 'formal_parameter')       {&p($a->{type})}
  elsif ($t eq 'unspecified_parameters') {'...'}
  else                                   {die "$ARGV: unknown debug info `$t'\n"}
};

my $format = 'objdump';
my @enums;
my @structs;
my @prefix;
my $write_defs;
my @read_defs;
my $output;
my @libs;
my @requires;

GetOptions(
  'format=s'     => \$format,
  'write-defs=s' => \$write_defs,
  'read-defs=s'  => \@read_defs,
  'enum=s'       => \@enums,
  'struct=s'     => \@structs,
  'prefix=s'     => \@prefix,
  'output=s'     => \$output,
  'load=s'       => \@libs,
  'require=s'    => \@requires,
) or exit 1;

if (defined $output) {
  open STDOUT, '>', $output or die "$output: $!\n";
}

foreach (@read_defs) {
  open my $file, '<', $_ or die "$_: $!\n";
  while (<$file>) {
    chomp;
    $defs{$_} = 1;
  }
  close $file;
}

my %parse_input = (
  'objdump' => \&parse_input_objdump,
  'eu-readelf' => \&parse_input_eu_readelf,
  'llvm-dwarfdump' => \&parse_input_llvm_dwarfdump,
);

my $info = &{$parse_input{$format}}();

print "local ffi = require(\"ffi\")\n";
print "ffi.cdef [[\n";

$_ =~ s/\*/.+/ foreach @enums;
$_ =~ s/\*/.+/ foreach @structs;

&p($_) for
  sort {$a->{attr}{name} cmp $b->{attr}{name}}
  grep {
    my $n = $_->{attr}{name} || '';
    ($_->{type} eq 'structure_type' and grep {$n =~ m/^$_$/} @structs) or
    ($_->{type} eq 'enumeration_type' and grep {$n =~ m/^$_$/} @enums)
  } values %$info;

print &p($_), ";\n" for
  sort {$a->{attr}{name} cmp $b->{attr}{name}}
  map {$defs{$_->{attr}{name}} = 2; $_}
  grep {my $n = $_->{attr}{name}; @prefix == 0 or grep {$n =~ /^$_/} @prefix}
  grep {not $defs{$_->{attr}{name}}}
  grep {$_->{attr}{external} and not $_->{attr}{declaration}}
  grep {$_->{type} eq 'subprogram'}
  values %$info;

print "]]\n";
print "require \"$_\"\n" foreach @requires;

foreach (@libs) {
  my $var = s/[-.]/_/gr;
  print "$var = ffi.load(\"$_\", true)\n";
}

if (defined $write_defs) {
  open my $file, '>', $write_defs or die "$write_defs: $!\n";
  print $file "$_\n" for sort grep {$defs{$_} == 2} keys %defs;
  close $file;
}
