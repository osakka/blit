#!/usr/bin/env ruby
# frozen_string_literal: true

# ADR 0243 step 1: one font registry. assets/fonts/fonts.source.json declares
# every face Blit ships, and nothing else names one:
#   * src/blit_font_registry.h is exactly what tools/fonts/build-font-registry.py
#     writes from the registry (the committed header is not edited by hand)
#   * every face has an id, a role and a draw from the registry's vocabulary,
#     every role has a face, and every face's file is there with the SHA-256
#     the registry pins (and its licence, where it names one)
#   * no face's file name is written anywhere in src/ but the generated header
#   * GET /v1/fonts reports the registry, face for face, every role in it

require 'digest'
require 'json'
require 'net/http'
require 'open3'
require 'timeout'
require 'tmpdir'
require_relative 'gate_port'
require_relative 'gate_server'

ROOT = File.expand_path('../..', __dir__)
FONTS = File.join(ROOT, 'assets/fonts')
HEADER = File.join(ROOT, 'src/blit_font_registry.h')
BINARY = ENV.fetch('BLIT_GATE_BINARY', File.join(ROOT, 'build/blit/blit-server'))
ROLES = %w[text icons nerd symbols unifont colour text_emoji arabic_words
           pixel_fallback pixel_wide].freeze
DRAWS = %w[cell fitted words colour].freeze

def check(ok, why)
  raise "FONT_REGISTRY_FAIL #{why}" unless ok
end

def faces(node, &block)
  case node
  when Hash
    return yield(node) if node.key?('role')

    node.each_value { |value| faces(value, &block) }
  when Array
    node.each { |value| faces(value, &block) }
  end
end

registry = JSON.parse(File.read(File.join(FONTS, 'fonts.source.json')))
check(!File.exist?(File.join(FONTS, 'arabic-fonts.source.json')),
      'arabic-fonts.source.json is back: the registry is fonts.source.json alone')

# The header is the generator's output.
generated, status = Open3.capture2('python3', '-I', File.join(ROOT, 'tools/fonts/build-font-registry.py'), '-')
check(status.success?, 'tools/fonts/build-font-registry.py failed')
check(generated == File.read(HEADER),
      'src/blit_font_registry.h is not the generator\'s output: run tools/fonts/build-font-registry.py src/blit_font_registry.h')

# Every face: its fields, its vocabulary, its pinned file.
table = []
faces(registry) { |face| table << face }
check(registry.dig('registry', 'roles').keys.sort == ROLES.sort, "registry roles #{registry.dig('registry', 'roles').keys}")
check(registry.dig('registry', 'draws').keys.sort == DRAWS.sort, "registry draws #{registry.dig('registry', 'draws').keys}")
table.each do |face|
  check(%w[id role draw file sha256].all? { |key| face[key].is_a?(String) && !face[key].empty? }, "incomplete face #{face}")
  check(ROLES.include?(face['role']) && DRAWS.include?(face['draw']), "#{face['id']}: #{face['role']}/#{face['draw']}")
  check(face['id'] == face['id'].downcase.scan(/[a-z0-9]+/).join('-'), "#{face['id']} is not kebab")
  path = File.join(FONTS, face['file'])
  check(File.file?(path) && Digest::SHA256.file(path).hexdigest == face['sha256'], "#{face['file']} is not the pinned file")
  next unless face['license_file']

  # Blit's own icons' licence is pinned with their sources (custom_icons).
  licence = File.join(FONTS, face['license_file'])
  pin = face['license_sha256'] || registry.dig('custom_icons', 'sources', face['license_file'])
  check(File.file?(licence) && Digest::SHA256.file(licence).hexdigest == pin,
        "#{face['license_file']} is not the pinned licence")
end
check(table.map { |f| f['id'] }.uniq.size == table.size, 'two faces share an id')
check(table.map { |f| f['file'] }.uniq.size == table.size, 'two faces share a file')
check((ROLES - table.map { |f| f['role'] }).empty?, "roles with no face: #{ROLES - table.map { |f| f['role'] }}")
pinned_text = Dir.glob('text/**/*', base: FONTS).select { |f| File.file?(File.join(FONTS, f)) && f =~ /\.(ttf|otf)\z/ }.sort
check(table.select { |f| f['role'] == 'text' }.map { |f| f['file'] }.sort == pinned_text, 'text/ and the registry disagree')
shipped = Dir.glob('*.{ttf,otf,psf}', base: FONTS).sort
check(table.reject { |f| f['role'] == 'text' }.map { |f| f['file'] }.sort == shipped,
      "faces in the fonts root and the registry disagree: #{shipped - table.map { |f| f['file'] }}")

# No face is named in src/ but by the generated header.
names = table.map { |f| File.basename(f['file']) } + [registry.dig('custom_icons', 'catalogue')]
sources = Dir[File.join(ROOT, 'src/*.{c,h}')].reject { |f| f == HEADER }
named = sources.flat_map do |source|
  text = File.read(source)
  names.select { |name| text.include?(name) }.map { |name| "#{File.basename(source)}: #{name}" }
end
check(named.empty?, "face files named outside the registry header: #{named.join(', ')}")
check(sources.count { |f| File.read(f).include?('#include "blit_font_registry.h"') } >= 3,
      'the font code does not include the registry header')

# The server reports the registry.
check(File.executable?(BINARY), "needs #{BINARY} (tools/build-tcc.sh)")
port = gate_port(Integer(ENV.fetch('BLIT_GATE_PORT', '19557'), 10))
Dir.mktmpdir('blit-font-registry') do |root|
  env = gate_display_env(width: 640, height: 384).merge(
    'BLIT_PSF_FONT' => File.join(FONTS, 'Blit-Terminus16.psf'), 'BLIT_FONT_ROOT' => FONTS,
    'BLIT_FONT_LOCAL_ROOT' => File.join(root, 'local'),
    'BLIT_LOGIN_AUTOSTART' => '0', 'BLIT_EXPECT_DESKTOP' => '0', 'BLIT_AUTH_TOKEN' => '',
    'BLIT_SETTINGS_PATH' => File.join(root, 'settings.json'))
  pid = gate_spawn(env, BINARY, port.to_s, out: File::NULL, err: File::NULL)
  begin
    get = lambda do |path|
      response = Net::HTTP.start('127.0.0.1', port, read_timeout: 20) { |h| h.get(path) }
      [response.code.to_i, response.body]
    end
    Timeout.timeout(15) { loop { break if (get.call('/v1/version').first rescue nil) == 200; sleep 0.1 } }
    code, body = get.call('/v1/fonts')
    check(code == 200, "GET /v1/fonts -> #{code}")
    reported = JSON.parse(body).fetch('registry')
    expected = table.map { |f| { 'id' => f['id'], 'role' => f['role'], 'draw' => f['draw'], 'file' => f['file'], 'family' => f['family'] } }
    check(reported == expected, "GET /v1/fonts registry differs: #{(reported - expected) + (expected - reported)}")
    check(reported.map { |f| f['role'] }.uniq.sort == ROLES.sort, "GET /v1/fonts roles #{reported.map { |f| f['role'] }.uniq}")
  ensure
    Process.kill('TERM', pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
puts "FONT_REGISTRY_PASS faces=#{table.size} roles=#{ROLES.size} header=generated hashes=exact " \
     'src_literals=none api=registry_every_role'
