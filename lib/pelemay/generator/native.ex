defmodule Pelemay.Generator.Native do
  alias Pelemay.Db
  alias Pelemay.Generator
  alias Pelemay.Generator.Native.Util, as: Util

  require Logger

  def generate(module) do
    Pelemay.Generator.libc(module) |> write(module)
  end

  defp write(file, module) do
    str =
      init_nif()
      |> generate_functions()
      |> erl_nif_init(module)

    file |> File.write(str)
  end

  defp generate_functions(str) do
    code_info = Db.get_functions()

    Db.clear()

    definition_func =
      code_info
      |> Enum.map(&generate_function(&1))
      |> Enum.filter(&(!is_nil(&1)))
      |> Enum.map(&(&1 <> "\n"))

    str <> Util.to_str_code(definition_func) <> func_list()
  end

  defp generate_function([func_info]) do
    generate_function(func_info)
  end

  defp generate_function(%{module: modules, function: funcs} = info) do
    object_module = Enum.reduce(modules, "", fn module, acc -> acc <> Atom.to_string(module) end)

    [hd | tl] = funcs

    acc =
      hd
      |> Keyword.keys()
      |> Enum.map(&Atom.to_string(&1))
      |> List.to_string()

    object_func =
      Enum.reduce(tl, acc, fn [{func, _}], acc -> acc <> "_" <> Atom.to_string(func) end)

    prefix = "Pelemay.Generator.Native.#{object_module}.#{object_func}"

    {res, _} =
      try do
        Code.eval_string("#{prefix}(info)", info: info)
      rescue
        e in UndefinedFunctionError ->
          Util.push_info(info, :impl, false)
          error(e)
      end

    res
  end

  defp func_list do
    fl =
      Db.get_functions()
      |> Enum.reduce(
        "",
        fn
          [%{impl: true, impl_drv: true}] = info, acc ->
            acc <> """
            #{erl_nif_func(info)},
            #{erl_nif_driver_double_func(info)},
            #{erl_nif_driver_i64_func(info)},
            """

          [%{impl: true, impl_drv: false}] = info, acc ->
            acc <> """
            #{erl_nif_func(info)},
            """

          [%{impl: false}], acc ->
            acc
        end
      )

    """
    static
    ErlNifFunc nif_funcs[] =
    {
      // {erl_function_name, erl_function_arity, c_function}
      #{fl}
    };

    """
  end

  defp erl_nif_func([%{nif_name: nif_name, arg_num: num}]) do
    ~s/{"#{nif_name}_nif", #{num}, #{nif_name}_nif}/
  end

  defp erl_nif_driver_double_func([%{nif_name: nif_name}]) do
    ~s/{"#{nif_name}_nif_driver_double", 1, #{nif_name}_nif_driver_double}/
  end

  defp erl_nif_driver_i64_func([%{nif_name: nif_name}]) do
    ~s/{"#{nif_name}_nif_driver_i64", 1, #{nif_name}_nif_driver_i64}/
  end
  defp init_nif do
    """
    // This file was generated by Pelemay.Generator.Native
    #pragma clang diagnostic ignored "-Wnullability-completeness"
    #pragma clang diagnostic ignored "-Wnullability-extension"

    #include <stdbool.h>
    #include <erl_nif.h>
    #include <string.h>
    #include "basic.h"


    static int load(ErlNifEnv *env, void **priv, ERL_NIF_TERM info);
    static void unload(ErlNifEnv *env, void *priv);
    static int reload(ErlNifEnv *env, void **priv, ERL_NIF_TERM info);
    static int upgrade(ErlNifEnv *env, void **priv, void **old_priv, ERL_NIF_TERM info);

    static int
    load(ErlNifEnv *env, void **priv, ERL_NIF_TERM info)
    {
      atom_struct = enif_make_atom(env, "__struct__");
      atom_range = enif_make_atom(env, "Elixir.Range");
      atom_first = enif_make_atom(env, "first");
      atom_last = enif_make_atom(env, "last");
      return 0;
    }

    static void
    unload(ErlNifEnv *env, void *priv)
    {
    }

    static int
    reload(ErlNifEnv *env, void **priv, ERL_NIF_TERM info)
    {
      return 0;
    }

    static int
    upgrade(ErlNifEnv *env, void **priv, void **old_priv, ERL_NIF_TERM info)
    {
      return load(env, priv, info);
    }

    """
  end

  defp erl_nif_init(str, module) do
    str <>
      """
      ERL_NIF_INIT(Elixir.#{Generator.nif_module(module)}, nif_funcs, &load, &reload, &upgrade, &unload)
      """
  end

  defp error(e) do
    Logger.warn(
      "Please write a native code of the following code: #{e.module}.#{e.function}/#{e.arity}"
    )

    {nil, []}
  end
end
