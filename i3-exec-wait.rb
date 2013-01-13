#!/usr/bin/env ruby
#
# Execute a command and wait for i3 to register window(s).
# I know it is ugly.
#
# http://www.drbig.one.pl
#

%w{bindata json socket}.each {|g| require g }

# data structures
class I3Head < BinData::Record
  endian  :little
  int32   :len
  int32   :type
end

class I3Data < BinData::Record
  endian  :little
  string  :magic, :initial_value => 'i3-ipc'
  int32   :len, :value => lambda { payload.length }
  int32   :type
  string  :payload, :read_length => :len
end

# print help message
def help
  STDERR.puts 'Usage: i3-exec-wait.rb (-n <how_many_windows> | -r <regexp>) command'
  STDERR.puts 'Without options it will just wait for any window to register.'
  STDERR.puts 'The -n option will wait for a number of windows to register.'
  STDERR.puts 'With a regexp it will wait for a window whose _initial_ title matches the regexp - beware that initial title may be different from what you see (e.g. opening urxvt or xterm with shell that changes window title).'
  exit(2)
end

# parse an array to a command string preserving spaces
def parse_cmd(array)
  array.collect do |e|
    if e.chars.to_a.include? ' '
      '"' + e + '"'
    else
      e
    end
  end.join(' ')
end

# get a message from i3-ipc
# or bail put saying why
def get_msg(socket)
  unless socket.read(6) == 'i3-ipc'
    STDERR.puts 'Wrong magic in response, bye.'
    exit(2)
  end

  head = I3Head.read(socket.read(8))
  begin
    payload = JSON.parse(socket.read(head.len))
  rescue
    STDERR.puts 'Error on getting or parsing payload, bye.'
    exit(2)
  end

  payload
end

# handle arguments, what a mess
# try to be nice and say what's wrong
if ARGV.length == 0
  help
else
  mode = false
  nwin = false
  regexp = false
  cmd = false

  if %w{--help -help -h}.member? ARGV.first
    help
  elsif ARGV.first == '-n'
    mode = :nwin
    begin
      nwin = ARGV[1].to_i
    rescue
      STDERR.puts 'Argument to -n should be an integer, sorry.'
      exit(2)
    end
    if ARGV.length < 3
      STDERR.puts 'You need to specify a command...'
      exit(2)
    end
    cmd = parse_cmd(ARGV.slice(2, ARGV.length-1))
  elsif ARGV.first == '-r'
    mode = :regexp
    begin
      regexp = Regexp.new(ARGV[1])
    rescue
      STDERR.puts 'Argument to -r should be a valid (Ruby) regexp, sorry.'
    end
    if ARGV.length < 3
      STDERR.puts 'You need to specify a command...'
      exit(2)
    end
    cmd = parse_cmd(ARGV.slice(2, ARGV.length-1))
  else
    mode = :simple
    cmd = parse_cmd(ARGV)
  end
end

UNIXSocket.open(%x{i3 --get-socketpath}.chop) do |s|
  s.write(I3Data.new(:type => 2, :payload => %w{window}.to_json).to_binary_s)
  unless get_msg(s)['success']
    STDERR.puts 'Could not subscribe to window events, bye.'
    exit(2)
  end

  begin
    Process.detach(Process.spawn(cmd))
  rescue
    STDERR.puts 'Error executing your command, bye.'
    exit(2)
  end

  case mode
  when :simple
    get_msg(s)
  when :nwin
    while nwin > 0
      get_msg(s)
      nwin -= 1
    end
  when :regexp
    while not get_msg(s)['container']['name'].match regexp
      # nothing
    end
  end

  exit(0)
end
