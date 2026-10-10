#!/usr/bin/env ruby
# frozen_string_literal: true

# Keep the machine-readable contract aligned with the implemented gateway
# routes. The OpenAPI document is authoritative; this gate only checks the
# routing invariants that a generic YAML parser can verify without generated
# bindings.

require 'yaml'

schema_path = ARGV.fetch(0, 'openapi/blit-0.1.yaml')
schema = YAML.load_file(schema_path)
raise 'schema is not OpenAPI 3.1' unless schema.fetch('openapi') == '3.1.0'

paths = schema.fetch('paths')
raise "unexpected path count #{paths.length}" unless paths.length == 34

session_path = paths.fetch('/v1/terminal-sessions/{session_id}')
raise 'session path must not accept terminal input directly' if session_path.key?('post')
raise 'session path lost GET' unless session_path.key?('get')
raise 'session path lost PATCH' unless session_path.key?('patch')
raise 'session path lost DELETE' unless session_path.key?('delete')

input_path = paths.fetch('/v1/terminal-sessions/{session_id}/input')
post = input_path.fetch('post')
raise 'terminal input operation id changed' unless post.fetch('operationId') == 'sendTerminalInput'
raise 'terminal input schema changed' unless
  post.dig('requestBody', 'content', 'application/json', 'schema', '$ref') ==
  '#/components/schemas/TerminalInput'
raise 'terminal input receipt schema changed' unless
  post.dig('responses', '202', 'content', 'application/json', 'schema', '$ref') ==
  '#/components/schemas/TerminalInputReceipt'

puts "API_SCHEMA_PASS paths=#{paths.length} terminal_input=/v1/terminal-sessions/{session_id}/input"
