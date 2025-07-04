#!/usr/bin/env python3
# Copyright 2024 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import web_idl_schema

from web_idl_schema import SchemaCompilerError

# Helper functions for fetching specific parts of a processed API schema
# dictionary.


def getFunction(schema: dict, name: str) -> dict:
  """Gets the function dictionary with the specified name from the schema.

  Args:
    schema: The processed API schema dictionary to look for the function in.
    name: The name of the function to look for.

  Returns:
    The dictionary for the function with the specified name.

  Raises:
    KeyError: If the given function name was not found in the list of functions.
  """
  for item in schema['functions']:
    if item['name'] == name:
      return item
  raise KeyError('Could not find "function" with name "%s" in schema' % name)


def getType(schema: dict, name: str) -> dict:
  """Gets the custom type dictionary with the specified name from the schema.

  Args:
    schema: The processed API schema dictionary to look for the type in.
    name: The name of the custom type to look for.

  Returns:
    The dictionary for the custom type with the specified name.

  Raises:
    KeyError: If the given type name was not found in the list of types.
  """
  for item in schema['types']:
    if item['id'] == name:
      return item
  raise KeyError('Could not find "type" with id "%s" in schema' % name)


def getEvent(schema: dict, name: str) -> dict:
  """Gets the event dictionary with the specified name from the schema.

  Args:
    schema: The processed API schema dictionary to look for the event in.
    name: The name of the event to look for.

  Returns:
    The dictionary for the event with the specified name.

  Raises:
    KeyError: If the given event name was not found in the list of events.
  """
  for item in schema['events']:
    if item['name'] == name:
      return item
  raise KeyError('Could not find "event" with name "%s" in schema' % name)


def getFunctionReturn(schema: dict, name: str) -> dict:
  """Gets the return dictionary for the function with the specified name.

  Args:
    schema: The processed API schema dictionary to look for the function in.
    name: The name of the function to get the return value from.

  Returns:
    The dictionary representing the return for the specified function name if it
    has a return, otherwise None if it does not.
  """
  function = getFunction(schema, name)
  return function.get('returns', None)


def getFunctionAsyncReturn(schema: dict, name: str) -> dict:
  """Gets the async return dictionary for the function with the specified name.

  Args:
    schema: The processed API schema dictionary to look for the function in.
    name: The name of the function to get the async return value from.

  Returns:
    The dictionary representing the async return for the function with the
    specified name if it has one, otherwise None if it does not.
  """
  function = getFunction(schema, name)
  return function.get('returns_async', None)


def getFunctionParameters(schema: dict, name: str) -> dict:
  """Gets the list of parameters for the function with the specified name.

  Args:
    schema: The processed API schema dictionary to look for the function in.
    name: The name of the function to get the parameters list from.

  Returns:
    The list of dictionaries representing the function parameters for the
    function with the specified name if it has any, otherwise None if it does
    not.
  """
  # TODO(crbug.com/340297705): All functions should have the 'parameters' key,
  # so we shouldn't have a None fallback and just raise a KeyError if it isn't
  # present.
  function = getFunction(schema, name)
  return function.get('parameters', None)


class WebIdlSchemaTest(unittest.TestCase):

  def setUp(self):
    loaded = web_idl_schema.Load('test/web_idl/basics.idl')
    self.assertEqual(1, len(loaded))
    self.assertEqual('testWebIdl', loaded[0]['namespace'])
    self.idl_basics = loaded[0]

  def testFunctionReturnTypes(self):
    schema = self.idl_basics
    # Test basic types.
    self.assertEqual(
        None,
        getFunctionReturn(schema, 'returnsUndefined'),
    )
    self.assertEqual(
        {
            'name': 'returnsBoolean',
            'type': 'boolean'
        },
        getFunctionReturn(schema, 'returnsBoolean'),
    )
    self.assertEqual(
        {
            'name': 'returnsDouble',
            'type': 'number'
        },
        getFunctionReturn(schema, 'returnsDouble'),
    )
    self.assertEqual(
        {
            'name': 'returnsLong',
            'type': 'integer'
        },
        getFunctionReturn(schema, 'returnsLong'),
    )
    self.assertEqual(
        {
            'name': 'returnsDOMString',
            'type': 'string'
        },
        getFunctionReturn(schema, 'returnsDOMString'),
    )
    self.assertEqual({
        'name': 'returnsCustomType',
        '$ref': 'ExampleType'
    }, getFunctionReturn(schema, 'returnsCustomType'))
    self.assertEqual(
        {
            'name': 'returnsDOMStringSequence',
            'type': 'array',
            'items': {
                'type': 'string'
            }
        }, getFunctionReturn(schema, 'returnsDOMStringSequence'))
    self.assertEqual(
        {
            'name': 'returnsCustomTypeSequence',
            'type': 'array',
            'items': {
                '$ref': 'ExampleType'
            }
        }, getFunctionReturn(schema, 'returnsCustomTypeSequence'))

  def testPromiseBasedReturn(self):
    schema = self.idl_basics
    self.assertEqual(
        {
            'name': 'callback',
            'optional': True,
            'parameters': [{
                'type': 'string'
            }],
            'type': 'promise'
        }, getFunctionAsyncReturn(schema, 'stringPromiseReturn'))
    self.assertEqual(
        {
            'name': 'callback',
            'optional': True,
            'parameters': [{
                'optional': True,
                'type': 'string'
            }],
            'type': 'promise'
        }, getFunctionAsyncReturn(schema, 'nullablePromiseReturn'))
    self.assertEqual(
        {
            'name': 'callback',
            'optional': True,
            'parameters': [{
                '$ref': 'ExampleType'
            }],
            'type': 'promise'
        }, getFunctionAsyncReturn(schema, 'customTypePromiseReturn'))
    self.assertEqual(
        {
            'name': 'callback',
            'optional': True,
            'parameters': [],
            'type': 'promise'
        }, getFunctionAsyncReturn(schema, 'undefinedPromiseReturn'))
    self.assertEqual(
        {
            'name': 'callback',
            'optional': True,
            'parameters': [{
                'type': 'array',
                'items': {
                    'type': 'integer'
                }
            }],
            'type': 'promise'
        }, getFunctionAsyncReturn(schema, 'longSequencePromiseReturn'))
    self.assertEqual(
        {
            'name': 'callback',
            'optional': True,
            'parameters': [{
                'type': 'array',
                'items': {
                    '$ref': 'ExampleType'
                }
            }],
            'type': 'promise'
        }, getFunctionAsyncReturn(schema, 'customTypeSequencePromiseReturn'))

  # Tests function parameters are processed as expected.
  def testFunctionParameters(self):
    schema = self.idl_basics
    # A function with no arguments has an empty array on the "parameters" key.
    self.assertEqual([], getFunctionParameters(schema, 'takesNoArguments'))

    self.assertEqual([{
        'name': 'stringArgument',
        'type': 'string'
    }], getFunctionParameters(schema, 'takesDOMString'))
    self.assertEqual([{
        'name': 'optionalBoolean',
        'optional': True,
        'type': 'boolean'
    }], getFunctionParameters(schema, 'takesOptionalBoolean'))
    self.assertEqual([{
        'name': 'argument1',
        'type': 'string'
    }, {
        'name': 'argument2',
        'optional': True,
        'type': 'number'
    }], getFunctionParameters(schema, 'takesMultipleArguments'))
    self.assertEqual([{
        'name': 'first',
        'type': 'string'
    }, {
        'name': 'optionalInner',
        'optional': True,
        'type': 'string'
    }, {
        'name': 'last',
        'type': 'string'
    }], getFunctionParameters(schema, 'takesOptionalInnerArgument'))
    self.assertEqual([{
        'name': 'sequenceArgument',
        'type': 'array',
        'items': {
            'type': 'boolean'
        }
    }], getFunctionParameters(schema, 'takesSequenceArgument'))
    self.assertEqual([{
        'name': 'optionalSequenceArgument',
        'type': 'array',
        'optional': True,
        'items': {
            'type': 'boolean'
        }
    }], getFunctionParameters(schema, 'takesOptionalSequenceArgument'))
    self.assertEqual([{
        'name': 'customTypeArgument',
        '$ref': 'ExampleType'
    }], getFunctionParameters(schema, 'takesCustomType'))
    self.assertEqual([{
        'name': 'optionalCustomTypeArgument',
        'optional': True,
        '$ref': 'ExampleType'
    }], getFunctionParameters(schema, 'takesOptionalCustomType'))

  # Tests function descriptions are processed as expected.
  # TODO(crbug.com/379052294): Add testcases for function return descriptions
  # once support for those are added to the processor.
  def testFunctionDescriptions(self):
    schema = self.idl_basics
    # A function without a preceding comment has no 'description' key.
    self.assertTrue('description' not in getFunction(schema, 'noDescription'))

    self.assertEqual(
        'One line description.',
        getFunction(schema, 'oneLineDescription').get('description'))
    self.assertEqual(
        'Multi line description. Split over. Multiple lines.',
        getFunction(schema, 'multiLineDescription').get('description'))
    self.assertEqual(
        '<p>Paragraphed description.</p><p>With blank comment line for'
        ' paragraph tags.</p>',
        getFunction(schema, 'paragraphedDescription').get('description'))

    function = getFunction(schema, 'parameterComments')
    self.assertEqual('This function has parameter comments.',
                     function.get('description'))
    function_parameters = getFunctionParameters(schema, 'parameterComments')
    self.assertEqual(2, len(function_parameters))
    self.assertEqual(
        {
            'description':
            ('This comment about the argument is split across multiple lines'
             ' and contains <em>HTML tags</em>.'),
            'name':
            'arg1',
            'type':
            'boolean',
        },
        function_parameters[0],
    )
    self.assertEqual(
        {
            'description': 'This second argument uses a custom type.',
            'name': 'arg2',
            '$ref': 'ExampleType'
        }, function_parameters[1])

    promise_function = getFunction(schema, 'namedPromiseReturn')
    self.assertEqual(
        ('Promise returning function, with a comment that provides the name and'
         ' description of the value the promise resolves to.'),
        promise_function.get('description'))
    promise_function_parameters = getFunctionParameters(schema,
                                                        'namedPromiseReturn')
    self.assertEqual(1, len(promise_function_parameters))
    self.assertEqual(
        {
            'description': 'This is a normal argument comment.',
            'name': 'arg1',
            'type': 'boolean',
        },
        promise_function_parameters[0],
    )
    promise_function_async_return = getFunctionAsyncReturn(
        schema, 'namedPromiseReturn')
    self.assertEqual(
        {
            'name':
            'callback',
            'optional':
            True,
            'type':
            'promise',
            'parameters': [{
                '$ref':
                'ExampleType',
                'name':
                'returnValueName',
                'description':
                ('A description for the value the promise resolves to: with'
                 ' an extra colon for good measure.'),
            }],
        },
        promise_function_async_return,
    )

  # Tests that API events are processed as expected.
  def testEvents(self):
    schema = self.idl_basics

    event_one = getEvent(schema, 'onTestOne')
    # This is a bit of a tautology for now, as getEvent() uses name to retrieve
    # the object and raises a KeyError if it is not found.
    self.assertEqual('onTestOne', event_one.get('name'))
    self.assertEqual('function', event_one.get('type'))
    self.assertEqual(
        'Comment that acts as a description for onTestOne. Parameter specific'
        ' comments are down below before the associated callback definition.',
        event_one.get('description'))
    self.assertEqual(
        [{
            'name': 'argument1',
            'type': 'string',
            'description': 'Parameter description for argument1.'
        }, {
            'name': 'argument2',
            'optional': True,
            'type': 'number',
            'description': 'Another description, this time for argment2.'
        }], event_one['parameters'])

    event_two = getEvent(schema, 'onTestTwo')
    self.assertEqual('onTestTwo', event_two.get('name'))
    self.assertEqual('function', event_two.get('type'))
    self.assertEqual('Comment for onTestTwo.', event_two.get('description'))
    self.assertEqual(
        [{
            'name': 'customType',
            '$ref': 'ExampleType',
            'description': 'An ExampleType passed to the event listener.'
        }], event_two['parameters'])

  # Tests that Dictionaries defined on the top level of the IDL file are
  # processed into types on the resulting namespace.
  def testApiTypesOnNamespace(self):
    schema = self.idl_basics
    custom_type = getType(schema, 'ExampleType')
    self.assertEqual('ExampleType', custom_type['id'])
    self.assertEqual('object', custom_type['type'])
    self.assertEqual(
        {
            'name': 'someString',
            'type': 'string',
            'description':
            'Attribute comment attached to ExampleType.someString.'
        }, custom_type['properties']['someString'])
    self.assertEqual(
        {
            'name': 'someNumber',
            'type': 'number',
            'description':
            'Comment where <var>someNumber</var> has some markup.'
        }, custom_type['properties']['someNumber'])
    # TODO(crbug.com/379052294): using HTML comments like this is a bit of a
    # hack to allow us to add comments in IDL files (e.g. for TODOs) and to not
    # have them end up on the documentation site. We should probably just filter
    # them out during compilation.
    self.assertEqual(
        {
            'name':
            'optionalBoolean',
            'type':
            'boolean',
            'optional':
            True,
            'description':
            'Comment with HTML comment. <!-- Which should get through -->'
        }, custom_type['properties']['optionalBoolean'])
    self.assertEqual(
        {
            'name': 'booleanSequence',
            'type': 'array',
            'items': {
                'type': 'boolean'
            },
            'description': 'Comment on sequence type.',
        }, custom_type['properties']['booleanSequence'])

  # Tests that a top level API comment is processed into a description
  # attribute, with HTML paragraph nodes added due to the blank commented line.
  def testApiDescriptionComment(self):
    schema = self.idl_basics
    expected_description = (
        '<p>This comment is an example of a top level API description, which'
        ' will be extracted and added to the processed python dictionary as a'
        ' description.</p><p>Note: All comment lines preceding the thing they'
        ' are attached to will be part of the description, until a blank new'
        ' line or non-comment is reached.</p>')
    self.assertEqual(expected_description, schema['description'])

  # Tests that if the nodoc extended attribute is not specified on the API
  # interface the related attribute is set to false after processing.
  def testNodocUnspecifiedOnNamespace(self):
    self.assertFalse(self.idl_basics['nodoc'])

  # TODO(crbug.com/340297705): This will eventually be relaxed when adding
  # support for shared types to the new parser.
  def testMissingBrowserInterfaceError(self):
    expected_error_regex = (
        r'.* File\(test\/web_idl\/missing_browser_interface.idl\): Required'
        r' partial Browser interface not found in schema\.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/missing_browser_interface.idl',
    )

  # Tests that having a Browser interface on an API definition with no attribute
  # throws an error.
  def testMissingAttributeOnBrowserError(self):
    expected_error_regex = (
        r'.* Interface\(Browser\): The partial Browser interface should have'
        r' exactly one attribute for the name the API will be exposed under\.')
    self.assertRaisesRegex(
        Exception,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/missing_attribute_on_browser.idl',
    )

  # Tests that using a valid basic WebIDL type with a "name" the schema compiler
  # doesn't support yet throws an error.
  def testUnsupportedBasicTypeError(self):
    expected_error_regex = (
        r'.* PrimitiveType\(float\): Unsupported basic type found when'
        r' processing type\.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/unsupported_basic_type.idl',
    )

  # Tests that using a valid WebIDL type with a node "class" the schema compiler
  # doesn't support yet throws an error.
  def testUnsupportedTypeClassError(self):
    expected_error_regex = (
        r'.* Any\(\): Unsupported type class when processing type\.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/unsupported_type_class.idl',
    )

  # Tests that an event trying to say it uses an Interface that is not defined
  # in the IDL file will throw an error. This is largely in place to help catch
  # spelling mistakes in event names or forgetting to add the Interface
  # definition.
  def testMissingEventInterface(self):
    expected_error_regex = (
        r'.* Error processing node Attribute\(onTestTwo\): Could not find'
        r' Interface definition for event\.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/missing_event_interface.idl',
    )

  # Various tests that ensure validation on event interface definitions.
  # Specifically checks that not defining any of the required add/remove/has
  # Operations or forgetting the ExtensionEvent inheritance will throw an error.
  def testMissingEventInheritance(self):
    expected_error_regex = (
        r'.* Error processing node Interface\(OnMissingInheritanceEvent\):'
        r' Event Interface missing ExtensionEvent Inheritance.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/missing_event_inheritance.idl',
    )

  def testMissingEventAddListener(self):
    expected_error_regex = (
        r'.* Error processing node Interface\(OnMissingAddListenerEvent\):'
        r' Event Interface missing addListener Operation definition.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/missing_event_add_listener.idl',
    )

  def testMissingEventRemoveListener(self):
    expected_error_regex = (
        r'.* Error processing node Interface\(OnMissingRemoveListenerEvent\):'
        r' Event Interface missing removeListener Operation definition.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/missing_event_remove_listener.idl',
    )

  def testMissingEventHasListener(self):
    expected_error_regex = (
        r'.* Error processing node Interface\(OnMissingHasListenerEvent\):'
        r' Event Interface missing hasListener Operation definition.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/missing_event_has_listener.idl',
    )

  # Tests that if description parsing from file comments reaches the top of the
  # file, a schema compiler error is thrown (as the top of the file should
  # always be copyright lines and not part of the description).
  def testDocumentationCommentReachedTopOfFileError(self):
    expected_error_regex = (
        r'.* Reached top of file when trying to parse description from file'
        r' comment. Make sure there is a blank line before the comment.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/documentation_comment_top_of_file.idl',
    )

  # Tests that usage of the 'void' type will result in a schema compiler error.
  # 'void' has been deprecated and 'undefined' should be used instead.
  def testVoidUsageTriggersError(self):
    expected_error_regex = (
        r'Error processing node PrimitiveType\(void\): Usage of "void" in IDL'
        r' is deprecated, use "Undefined" instead.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/void_unsupported.idl',
    )

  # Tests that a namespace with an extended attribute that we don't have
  # processing for results in a schema compiler error.
  def testUnknownNamespaceExtendedAttributeNameError(self):
    expected_error_regex = (
        r'.* Interface\(TestWebIdl\): Unknown extended attribute with name'
        r' "UnknownExtendedAttribute" when processing namespace.')
    self.assertRaisesRegex(
        SchemaCompilerError,
        expected_error_regex,
        web_idl_schema.Load,
        'test/web_idl/unknown_namespace_extended_attribute.idl',
    )

  # Tests that an API interface that uses the nodoc extended attribute has the
  # related nodoc attribute set to true after processing.
  def testNoDocOnNamespace(self):
    idl = web_idl_schema.Load('test/web_idl/nodoc_on_namespace.idl')
    self.assertEqual(1, len(idl))
    schema = idl[0]
    self.assertEqual('nodocAPI', schema['namespace'])
    self.assertTrue(schema['nodoc'])
    # Also ensure the description comes through correctly on the node with
    # 'nodoc' as an extended attribute.
    self.assertEqual(
        'The nodoc API. This exists to demonstrate nodoc on the main interface'
        ' itself.',
        schema['description'],
    )

  # Tests that a function defined with the requiredCallback extended attribute
  # does not have the returns_async field marked as optional after processing.
  # Note: These are only relevant to contexts which don't support promise based
  # calls, or for specific functions which still do not support promises.
  def testRequiredCallbackFunction(self):
    idl = web_idl_schema.Load('test/web_idl/required_callback_function.idl')
    self.assertEqual(1, len(idl))
    self.assertEqual(
        {
            'name': 'callback',
            'parameters': [{
                'type': 'string'
            }],
            'type': 'promise'
        }, getFunctionAsyncReturn(idl[0], 'requiredCallbackFunction'))
    self.assertEqual(
        {
            'name': 'callback',
            'optional': True,
            'parameters': [{
                'type': 'string'
            }],
            'type': 'promise'
        }, getFunctionAsyncReturn(idl[0], 'notRequiredCallbackFunction'))

  # Tests that extended attributes being listed on the the line previous to a
  # node come through correctly and don't throw off and associated descriptions.
  # TODO(crbug.com/340297705): Add checks for functions here once support for
  # processing their descriptions is complete.
  def testPreviousLineExtendedAttributes(self):
    idl = web_idl_schema.Load('test/web_idl/preceding_extended_attributes.idl')
    self.assertEqual(1, len(idl))
    schema = idl[0]
    self.assertEqual('precedingExtendedAttributes', schema['namespace'])
    self.assertTrue(schema['nodoc'])
    self.assertEqual(
        'Comment on a schema that has extended attributes on a previous line.',
        schema['description'],
    )

  # Tests that an API interface with the platforms extended attribute has these
  # values in a platforms attribute after processing.
  def testAllPlatformsOnNamespace(self):
    platforms_schema = web_idl_schema.Load(
        'test/web_idl/all_platforms_on_namespace.idl')
    self.assertEqual(1, len(platforms_schema))
    self.assertEqual('allPlatformsAPI', platforms_schema[0]['namespace'])
    expected = ['chromeos', 'desktop_android', 'fuchsia', 'linux', 'mac', 'win']
    self.assertEqual(expected, platforms_schema[0]['platforms'])

  # Tests that an API interface with just chromeos listed in the platforms
  # extended attribute just has that after processing.
  def testChromeOSPlatformsOnNamespace(self):
    platforms_schema = web_idl_schema.Load(
        'test/web_idl/chromeos_platforms_on_namespace.idl')
    self.assertEqual(1, len(platforms_schema))
    self.assertEqual('chromeOSPlatformsAPI', platforms_schema[0]['namespace'])
    expected = ['chromeos']
    self.assertEqual(expected, platforms_schema[0]['platforms'])

  # Tests a variety of default values that are set on an API namespace when they
  # are not specified in the source IDL file.
  def testNonSpecifiedDefaultValues(self):
    defaults_schema = web_idl_schema.Load('test/web_idl/defaults.idl')[0]
    self.assertEqual(
        {
            'compiler_options': {},
            'deprecated': None,
            'description': '',
            'events': [],
            'functions': [],
            'manifest_keys': None,
            'namespace': 'defaultsOnlyWebIdl',
            'nodoc': False,
            'platforms': None,
            'properties': {},
            'types': [],
        }, defaults_schema)


if __name__ == '__main__':
  unittest.main()
