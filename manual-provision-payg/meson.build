endless_factory_test_tar = custom_target('Endless_Factory_Test.tar',
  input:  [ 'start.sh', 'code_gen.py' ],
  output: 'Endless_Factory_Test.tar',
  command: [find_program('tar'),
            '-c', '-f', '@OUTPUT@',
            # Store 'start.sh' and 'code_gen.py'  in the archive, with no directory components
            '--transform=s,.*/,,',
            # Canonicalize metadata so the build is reproducible
            '--owner=0',
            '--group=0',
            '--mtime=1970-01-01 00:00:00Z',
            '--mode=755',
            '--sort=name',
            '@INPUT@',
          ],
)

endless_factory_test_tar_sha256 = custom_target('Endless_Factory_Test.tar.sha256',
  input: endless_factory_test_tar,
  output: 'Endless_Factory_Test.tar.sha256',
  command: [find_program('hide_provenance.sh'), '@INPUT@'],
  capture: true,
)

# Check that the committed copies of these files are up-to-date
diff = find_program('diff')
foreach pair: [
  ['Endless_Factory_Test.tar', endless_factory_test_tar],
  ['Endless_Factory_Test.tar.sha256', endless_factory_test_tar_sha256],
]
  test(pair[0] + '-up-to-date',
    diff,
    args: [files(pair[0]), pair[1]],
  )
endforeach
