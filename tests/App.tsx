/// <reference types="nativewind/types" />
import React, { useEffect, useState } from 'react';
import { SafeAreaView, ScrollView, Text } from 'react-native';
import 'reflect-metadata';
import 'react-native-get-random-values';

import { registerBaseTests, runTests } from './tests/index';
import { registerQueriesBaseTests } from './tests/sqlite/queries.spec';
const TEST_SERVER_URL = 'http://localhost:4243/results';

export default function App() {
  const [results, setResults] = useState<any>([]);

  const executeTests = React.useCallback(async () => {
    setResults([]);

    try {
      // let results = await runTests(registerBaseTests);
      // console.log(JSON.stringify(results, null, '\t'));
      // setResults(results);
      // // Send results to host server
      // await fetch(TEST_SERVER_URL, {
      //   method: 'POST',
      //   headers: { 'Content-Type': 'application/json' },
      //   body: JSON.stringify(results),
      // });

      setResults([]);
      let results1 = await runTests(registerQueriesBaseTests);
      console.log(JSON.stringify(results1, null, '\t'));
      setResults(results1);
      await fetch(TEST_SERVER_URL, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(results1),
      });
    } catch (ex) {
      console.error(ex);
      // Send results to host server
      fetch(TEST_SERVER_URL, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify([
          {
            description: `Caught exception: ${ex}`,
            type: 'incorrect',
          },
        ]),
      });
    }
  }, []);

  useEffect(() => {
    console.log('Running Tests:');
    executeTests();
  }, []);

  return (
    <SafeAreaView className="flex-1 bg-neutral-900">
      <ScrollView className="p-4">
        <Text className="font-bold text-blue-500 text-lg text-center">
          RN Quick SQLite Test Suite
        </Text>
        {results.map((r: any, i: number) => {
          if (r.type === 'grouping') {
            return (
              <Text key={i} className="mt-3 font-bold text-white">
                {r.description}
              </Text>
            );
          }

          if (r.type === 'incorrect') {
            return (
              <Text key={i} className="mt-1 text-white">
                🔴 {r.description}: {r.errorMsg}
              </Text>
            );
          }

          return (
            <Text key={i} className="mt-1 text-white">
              🟢 {r.description}
            </Text>
          );
        })}
      </ScrollView>
    </SafeAreaView>
  );
}
