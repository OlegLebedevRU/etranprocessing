# Accepted consumer fixtures

These immutable data copies make the MenuBuilder verification image independent
of neighboring working trees. They do not prove current provider runtime behavior.
Authorization: `R-L4D-18E-MB-FIX-01-v3`, controller commit
`8743eff635da35359def624725a2c67059610179`.

| Local file | Export commit | SHA-256 |
| --- | --- | --- |
| accepted-18a-schema.json | 7e1fc3c0eabcdb3fc333d0dbe88b938ae09acdab | 52f481dd3d9985c54b5388a1d9e63062a8fdbe626870b58a83b3461c3e08e49f |
| accepted-17f-archive-manifest.json | 088ade1279547af90f7d0e682547293768f453fb | 0fdf5c04e8373b66ce9a448f038881588c7aa2a04e7aad259ca390a2171a642f |
| accepted-public-hub-nginx-contract.json | 088ade1279547af90f7d0e682547293768f453fb | daf76685fe8037b13ef12aa49f2c5b7906949833523cc01a720f52785eae7d32 |

The schema comes from `acceptance-18e-mb-evidence-v1/18a-schema.json`.
The other files come from `acceptance-18e-mb-fixtures-v1/`.
The archive manifest is synthetic. The nginx JSON is a projection of the eight
required Hub directives; it is not a configuration file for deployment.
