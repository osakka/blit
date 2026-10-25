#!/usr/bin/env ruby
require 'json'
require 'net/http'
require 'tmpdir'
require 'timeout'
require_relative 'gate_port'
require_relative 'gate_server'
ROOT=File.expand_path('../..',__dir__)
PORT=gate_port(Integer(ENV.fetch('BLIT_GATE_PORT','19521')))
BASE=URI("http://127.0.0.1:#{PORT}")
def request(method,path,body=nil,revision=nil)
  q={'GET'=>Net::HTTP::Get,'PUT'=>Net::HTTP::Put,'POST'=>Net::HTTP::Post}.fetch(method).new(BASE+path)
  q['Content-Type']='application/json';q.body=body.is_a?(String) ? body : JSON.generate(body) if body
  q['If-Match']=revision.to_s if revision
  r=Net::HTTP.start(BASE.host,BASE.port,read_timeout:5){|h|h.request(q)}
  [r.code.to_i,JSON.parse(r.body)]
end
Dir.mktmpdir('blit-pointer-appearance') do |root|
  pid=nil
  begin
    start=-> do
      env={**gate_display_env,'BLIT_LOGIN_AUTOSTART'=>'0',
           'BLIT_SETTINGS_PATH'=>File.join(root,'settings.json'),'BLIT_INPUT_ROOT'=>root,
           'BLIT_PSF_FONT'=>File.join(ROOT,'assets/fonts/Blit-Hibr-Terminus16.psf'),
           }
      pid=spawn(env,ENV.fetch('BLIT_GATE_BINARY',File.join(ROOT,'build/blit/blit-server')),PORT.to_s,out:File.join(root,'server.log'),err:[:child,:out])
      Timeout.timeout(10){loop{begin;break if request('GET','/v1/version')[0]==200;rescue Errno::ECONNREFUSED;end;sleep 0.02}}
    end
    start.call
    initial=request('GET','/v1/settings')[1]
    expected={'shape'=>'arrow','palette'=>'pearl','size_px'=>24,'shadow'=>true}
    abort('POINTER_APPEARANCE_FAIL missing canonical defaults') unless initial.dig('effective','pointer')==expected
    nested=initial['document'].merge('extension'=>{'pointer'=>{'size_px'=>999}})
    code,reply=request('PUT','/v1/settings',{'document'=>nested},initial['revision'])
    abort("POINTER_APPEARANCE_FAIL nested extension shadowed appearance:#{code}") unless code==200 && reply.dig('effective','pointer')==expected && reply.dig('document','extension')==nested['extension']
    code,initial=request('PUT','/v1/settings',{'document'=>initial['document']},reply['revision'])
    abort('POINTER_APPEARANCE_FAIL reset extension') unless code==200
    initial=request('GET','/v1/settings')[1]
    duplicate=JSON.generate('document'=>initial['document'].merge('pointer'=>{})).sub('"pointer":{}','"pointer":{},"pointer":{"size_px":48}')
    code,reply=request('PUT','/v1/settings',duplicate,initial['revision'])
    abort('POINTER_APPEARANCE_FAIL duplicate appearance accepted') unless code==400 && reply['code']=='invalid_settings_pointer'
    after_duplicate=request('GET','/v1/settings')[1]
    abort('POINTER_APPEARANCE_FAIL duplicate mutated settings') unless after_duplicate==initial
    escaped_duplicate=duplicate.sub('"pointer":{"size_px":48}', '"point\u0065r":{"size_px":48}')
    code,reply=request('PUT','/v1/settings',escaped_duplicate,initial['revision'])
    abort('POINTER_APPEARANCE_FAIL escaped duplicate accepted') unless code==400 && reply['code']=='invalid_settings_pointer'
    escaped=JSON.generate('document'=>initial['document'].merge('pointer'=>{'size_px'=>32})).sub('"pointer":','"point\u0065r":').sub('"size_px":','"size_\u0070x":')
    code,reply=request('PUT','/v1/settings',escaped,initial['revision'])
    abort('POINTER_APPEARANCE_FAIL escaped appearance lost') unless code==200 && reply.dig('effective','pointer','size_px')==32
    code,_=request('PUT','/v1/settings',{'document'=>initial['document']},reply['revision'])
    abort('POINTER_APPEARANCE_FAIL reset escaped appearance') unless code==200
    initial=request('GET','/v1/settings')[1]
    invalid=[{'size_px'=>23},{'size_px'=>65},{'size_px'=>32.5},{'size_px'=>'32'},
             {'shape'=>'nope'},{'palette'=>'nope'},{'shadow'=>1},{'unknown'=>true},
             {'shape'=>"arrow\0suffix"},{'palette'=>"pearl\0suffix"},{"shape\0suffix"=>'arrow'},nil,[]]
    invalid.each do |bad|
      doc=initial['document'].merge('pointer'=>bad)
      code,reply=request('PUT','/v1/settings',{'document'=>doc},initial['revision'])
      abort("POINTER_APPEARANCE_FAIL invalid accepted:#{bad.inspect}:#{code}") unless code==400 && reply['code']=='invalid_settings_pointer'
      abort('POINTER_APPEARANCE_FAIL invalid mutated settings') unless request('GET','/v1/settings')[1]==initial
    end
    custom={'shape'=>'hand','palette'=>'neon','size_px'=>48,'shadow'=>false}
    code,saved=request('PUT','/v1/settings',{'document'=>initial['document'].merge('pointer'=>custom)},initial['revision'])
    abort("POINTER_APPEARANCE_FAIL save:#{code}:#{saved}") unless code==200 && saved.dig('effective','pointer')==custom && saved.dig('storage','state')=='clean'
    stale,_=request('PUT','/v1/settings',{'document'=>initial['document']},initial['revision'])
    abort('POINTER_APPEARANCE_FAIL stale overwrite') unless stale==409
    font=request('GET','/v1/fonts')[1]
    request('PUT','/v1/fonts',{'scale'=>2.0})
    abort('POINTER_APPEARANCE_FAIL pointer follows font') unless request('GET','/v1/settings')[1].dig('effective','pointer')==custom
    Process.kill('TERM',pid);Process.wait(pid);pid=nil
    start.call
    restored=request('GET','/v1/settings')[1]
    abort('POINTER_APPEARANCE_FAIL restart lost pointer') unless restored.dig('document','pointer')==custom && restored.dig('effective','pointer')==custom
    puts 'POINTER_APPEARANCE_PASS invalid=no_mutation scope=top_level duplicate=rejected stale=409 live=applied restart=durable font=independent'
  ensure
    Process.kill('TERM',pid) rescue nil
    Process.wait(pid) rescue nil
  end
end
